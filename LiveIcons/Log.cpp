#include "pch.h"
#include "Log.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
	constexpr std::size_t MaxPathCharacters = 32768;
	constexpr std::size_t RecordCapacity = 2048;
	constexpr std::size_t MaxDetailCharacters = 256;
	constexpr std::uint64_t MaxLogFileBytes = 2ULL * 1024ULL * 1024ULL;
	constexpr unsigned RetainedRotations = 3;
	constexpr unsigned MaxErrorRecordsPerWindow = 120;
	constexpr unsigned MaxStaleFilesInspected = 256;
	constexpr ULONGLONG RateLimitWindowMilliseconds = 60ULL * 1000ULL;
	constexpr ULONGLONG StaleLogAgeTicks = 14ULL * 24ULL * 60ULL * 60ULL * 10000000ULL;

	SRWLOCK LogLock = SRWLOCK_INIT;
	volatile LONG LogLockOwner{};
	alignas(8) volatile LONG64 CorrelationCounter{};
	bool InitializationAttempted{};
	bool LogPathReady{};
	wchar_t LogDirectory[MaxPathCharacters]{};
	wchar_t LogFilePath[MaxPathCharacters]{};
	// All accesses are protected by LogLock. Keeping these large buffers in the
	// data segment avoids consuming a thumbnail host thread's limited stack.
	wchar_t PathScratchOne[MaxPathCharacters]{};
	wchar_t PathScratchTwo[MaxPathCharacters]{};
	ULONGLONG ErrorRecordTimes[MaxErrorRecordsPerWindow]{};
	unsigned OldestErrorRecord{};
	unsigned ErrorRecordCount{};
	bool RateLimitMarkerWritten{};

	enum class Level : unsigned char
	{
		Diagnostic,
		Error,
		Exception
	};

	class LastErrorGuard final
	{
		DWORD SavedError;

	public:
		LastErrorGuard() noexcept : SavedError{ GetLastError() } { }
		LastErrorGuard(const LastErrorGuard&) = delete;
		LastErrorGuard& operator=(const LastErrorGuard&) = delete;
		~LastErrorGuard() noexcept { SetLastError(SavedError); }
	};

	class LogLockGuard final
	{
		DWORD ThreadId{};
		bool Acquired{};
		bool Recursive{};

	public:
		LogLockGuard() noexcept : ThreadId{ GetCurrentThreadId() }
		{
			if (static_cast<DWORD>(InterlockedCompareExchange(&LogLockOwner, 0, 0)) == ThreadId)
			{
				Recursive = true;
				return;
			}

			// Logging is best effort and must not serialize thumbnail requests behind
			// another request's filesystem I/O.
			if (!TryAcquireSRWLockExclusive(&LogLock))
				return;
			InterlockedExchange(&LogLockOwner, static_cast<LONG>(ThreadId));
			Acquired = true;
		}

		LogLockGuard(const LogLockGuard&) = delete;
		LogLockGuard& operator=(const LogLockGuard&) = delete;

		~LogLockGuard() noexcept
		{
			if (!Acquired)
				return;
			InterlockedExchange(&LogLockOwner, 0);
			ReleaseSRWLockExclusive(&LogLock);
		}

		[[nodiscard]] bool OwnsLock() const noexcept { return Acquired; }
		[[nodiscard]] bool IsRecursive() const noexcept { return Recursive; }
	};

	class RecordBuilder final
	{
		std::array<char, RecordCapacity> Buffer{};
		std::size_t Length{};
		bool Truncated{};

		static constexpr std::size_t ReservedTail = 24;

		void AppendCharacter(const char character) noexcept
		{
			if (Length >= Buffer.size() - ReservedTail)
			{
				Truncated = true;
				return;
			}
			Buffer[Length++] = character;
		}

		void AppendTail(const char* text) noexcept
		{
			if (text == nullptr)
				return;
			while (*text != '\0' && Length + 1 < Buffer.size())
				Buffer[Length++] = *text++;
		}

	public:
		void Append(const char* text) noexcept
		{
			if (text == nullptr)
				return;
			while (*text != '\0')
			{
				const auto previousLength = Length;
				AppendCharacter(*text++);
				if (Length == previousLength)
					break;
			}
		}

		void AppendDecimal(std::uint64_t value) noexcept
		{
			char digits[21]{};
			std::size_t count{};
			do
			{
				digits[count++] = static_cast<char>('0' + value % 10);
				value /= 10;
			} while (value != 0 && count < std::size(digits));

			while (count != 0)
			{
				--count;
				AppendCharacter(digits[count]);
			}
		}

		void AppendHex32(const std::uint32_t value) noexcept
		{
			constexpr char HexDigits[]{ "0123456789ABCDEF" };
			Append("0x");
			for (int shift = 28; shift >= 0; shift -= 4)
				AppendCharacter(HexDigits[(value >> shift) & 0x0f]);
		}

		void AppendQuotedDetail(const char* detail) noexcept
		{
			if (detail == nullptr || *detail == '\0')
				return;

			Append(" detail=\"");
			for (std::size_t index = 0; index < MaxDetailCharacters && detail[index] != '\0'; ++index)
			{
				const auto character = static_cast<unsigned char>(detail[index]);
				if (character == '\\' || character == '"')
				{
					AppendCharacter('\\');
					AppendCharacter(static_cast<char>(character));
				}
				else if (character >= 0x20 && character != 0x7f)
				{
					AppendCharacter(static_cast<char>(character));
				}
				else
				{
					AppendCharacter(' ');
				}
				if (Truncated)
					break;
			}
			AppendCharacter('"');
		}

		void Finish() noexcept
		{
			if (Truncated)
				AppendTail(" truncated=true");
			AppendTail("\r\n");
			Buffer[Length] = '\0';
		}

		[[nodiscard]] const char* Data() const noexcept { return Buffer.data(); }
		[[nodiscard]] DWORD Size() const noexcept { return static_cast<DWORD>(Length); }
	};

	[[nodiscard]] const char* EventName(const Log::EventId eventId) noexcept
	{
		switch (eventId)
		{
		case Log::EventId::Logger: return "logger";
		case Log::EventId::DllGetClassObject: return "dll-get-class-object";
		case Log::EventId::DllRegisterServer: return "dll-register-server";
		case Log::EventId::DllUnregisterServer: return "dll-unregister-server";
		case Log::EventId::ClassFactoryCreate: return "class-factory-create";
		case Log::EventId::ClassFactoryQueryInterface: return "class-factory-query-interface";
		case Log::EventId::ClassFactoryLockServer: return "class-factory-lock-server";
		case Log::EventId::LiveIconsCreate: return "live-icons-create";
		case Log::EventId::LiveIconsQueryInterface: return "live-icons-query-interface";
		case Log::EventId::LiveIconsInitialize: return "live-icons-initialize";
		case Log::EventId::Thumbnail: return "thumbnail";
		case Log::EventId::ComRelease: return "com-release";
		case Log::EventId::Registry: return "registry";
		case Log::EventId::DecodeBase64: return "decode-base64";
		default: return "unknown";
		}
	}

	[[nodiscard]] const char* LevelName(const Level level) noexcept
	{
		switch (level)
		{
		case Level::Diagnostic: return "diagnostic";
		case Level::Error: return "error";
		case Level::Exception: return "exception";
		default: return "unknown";
		}
	}

	[[nodiscard]] bool AppendPathComponent(
		wchar_t* path,
		const std::size_t capacity,
		const wchar_t* component) noexcept
	{
		if (path == nullptr || component == nullptr || capacity == 0)
			return false;

		const auto pathLength = wcsnlen_s(path, capacity);
		const auto componentLength = wcsnlen_s(component, capacity);
		if (pathLength >= capacity || componentLength >= capacity)
			return false;

		const bool separatorRequired = pathLength != 0 && path[pathLength - 1] != L'\\';
		const auto required = pathLength + (separatorRequired ? 1U : 0U) + componentLength + 1U;
		if (required > capacity)
			return false;

		auto offset = pathLength;
		if (separatorRequired)
			path[offset++] = L'\\';
		for (std::size_t index = 0; index < componentLength; ++index)
			path[offset++] = component[index];
		path[offset] = L'\0';
		return true;
	}

	[[nodiscard]] bool EnsureDirectory(const wchar_t* path) noexcept
	{
		if (CreateDirectoryW(path, nullptr))
			return true;
		if (GetLastError() != ERROR_ALREADY_EXISTS)
			return false;
		const auto attributes = GetFileAttributesW(path);
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	[[nodiscard]] bool CopyPath(
		wchar_t* destination,
		const std::size_t destinationCapacity,
		const wchar_t* source) noexcept
	{
		if (destination == nullptr || source == nullptr || destinationCapacity == 0)
			return false;
		const auto length = wcsnlen_s(source, destinationCapacity);
		if (length >= destinationCapacity)
			return false;
		for (std::size_t index = 0; index <= length; ++index)
			destination[index] = source[index];
		return true;
	}

	void DeleteStaleLogsLocked() noexcept
	{
		PathScratchOne[0] = L'\0';
		PathScratchTwo[0] = L'\0';
		if (!CopyPath(PathScratchOne, std::size(PathScratchOne), LogDirectory) ||
			!AppendPathComponent(PathScratchOne, std::size(PathScratchOne), L"LiveIcons-*.log*"))
			return;

		WIN32_FIND_DATAW fileData{};
		const auto search = FindFirstFileW(PathScratchOne, &fileData);
		if (search == INVALID_HANDLE_VALUE)
			return;

		FILETIME currentTime{};
		GetSystemTimeAsFileTime(&currentTime);
		ULARGE_INTEGER current{};
		current.LowPart = currentTime.dwLowDateTime;
		current.HighPart = currentTime.dwHighDateTime;

		unsigned inspected{};
		do
		{
			++inspected;
			if ((fileData.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
				continue;

			ULARGE_INTEGER lastWrite{};
			lastWrite.LowPart = fileData.ftLastWriteTime.dwLowDateTime;
			lastWrite.HighPart = fileData.ftLastWriteTime.dwHighDateTime;
			if (lastWrite.QuadPart > current.QuadPart ||
				current.QuadPart - lastWrite.QuadPart <= StaleLogAgeTicks)
				continue;

			if (CopyPath(PathScratchTwo, std::size(PathScratchTwo), LogDirectory) &&
				AppendPathComponent(PathScratchTwo, std::size(PathScratchTwo), fileData.cFileName))
				static_cast<void>(DeleteFileW(PathScratchTwo));
		} while (inspected < MaxStaleFilesInspected && FindNextFileW(search, &fileData));

		FindClose(search);
	}

	[[nodiscard]] bool InitializeLogPathLocked() noexcept
	{
		if (InitializationAttempted)
			return LogPathReady;
		InitializationAttempted = true;

		PWSTR localAppDataLow{};
		const HRESULT knownFolderResult = SHGetKnownFolderPath(
			FOLDERID_LocalAppDataLow, KF_FLAG_DEFAULT, nullptr, &localAppDataLow);
		const bool copiedKnownFolder = SUCCEEDED(knownFolderResult) &&
			localAppDataLow != nullptr &&
			CopyPath(LogDirectory, std::size(LogDirectory), localAppDataLow);
		CoTaskMemFree(localAppDataLow);

		if (!copiedKnownFolder)
		{
			const auto localAppDataLength = GetEnvironmentVariableW(
				L"LOCALAPPDATA", LogDirectory,
				static_cast<DWORD>(std::size(LogDirectory)));
			if (localAppDataLength == 0 || localAppDataLength >= std::size(LogDirectory))
				return false;
		}

		if (!AppendPathComponent(LogDirectory, std::size(LogDirectory), L"LiveIcons") ||
			!EnsureDirectory(LogDirectory) ||
			!AppendPathComponent(LogDirectory, std::size(LogDirectory), L"Logs") ||
			!EnsureDirectory(LogDirectory) ||
			!CopyPath(LogFilePath, std::size(LogFilePath), LogDirectory))
			return false;

		wchar_t fileName[64]{};
		if (_snwprintf_s(fileName, std::size(fileName), _TRUNCATE, L"LiveIcons-%lu.log",
			static_cast<unsigned long>(GetCurrentProcessId())) < 0 ||
			!AppendPathComponent(LogFilePath, std::size(LogFilePath), fileName))
			return false;

		DeleteStaleLogsLocked();
		LogPathReady = true;
		return true;
	}

	enum class RateLimitDecision : unsigned char
	{
		WriteRecord,
		WriteSuppressionMarker,
		Suppress
	};

	[[nodiscard]] RateLimitDecision ApplyRateLimitLocked(const Level level) noexcept
	{
		if (level == Level::Diagnostic)
			return RateLimitDecision::WriteRecord;

		const auto now = GetTickCount64();
		while (ErrorRecordCount != 0 &&
			now - ErrorRecordTimes[OldestErrorRecord] >= RateLimitWindowMilliseconds)
		{
			OldestErrorRecord = (OldestErrorRecord + 1) % MaxErrorRecordsPerWindow;
			--ErrorRecordCount;
		}
		if (ErrorRecordCount == 0)
			RateLimitMarkerWritten = false;

		if (ErrorRecordCount < MaxErrorRecordsPerWindow)
		{
			const auto insertionIndex =
				(OldestErrorRecord + ErrorRecordCount) % MaxErrorRecordsPerWindow;
			ErrorRecordTimes[insertionIndex] = now;
			++ErrorRecordCount;
			return RateLimitDecision::WriteRecord;
		}

		if (!RateLimitMarkerWritten)
		{
			RateLimitMarkerWritten = true;
			return RateLimitDecision::WriteSuppressionMarker;
		}
		return RateLimitDecision::Suppress;
	}

	[[nodiscard]] bool MakeRotatedPath(
		const unsigned rotation,
		wchar_t* destination,
		const std::size_t capacity) noexcept
	{
		if (!CopyPath(destination, capacity, LogFilePath))
			return false;
		const auto length = wcsnlen_s(destination, capacity);
		if (length >= capacity || length + 3 >= capacity || rotation > 9)
			return false;
		destination[length] = L'.';
		destination[length + 1] = static_cast<wchar_t>(L'0' + rotation);
		destination[length + 2] = L'\0';
		return true;
	}

	[[nodiscard]] bool DeleteIfPresent(const wchar_t* path) noexcept
	{
		if (DeleteFileW(path))
			return true;
		const auto error = GetLastError();
		return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
	}

	[[nodiscard]] bool MoveIfPresent(const wchar_t* source, const wchar_t* destination) noexcept
	{
		if (MoveFileExW(source, destination, MOVEFILE_REPLACE_EXISTING))
			return true;
		const auto error = GetLastError();
		return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
	}

	[[nodiscard]] bool RotateLogLocked() noexcept
	{
		PathScratchOne[0] = L'\0';
		PathScratchTwo[0] = L'\0';
		if (!MakeRotatedPath(RetainedRotations, PathScratchTwo, std::size(PathScratchTwo)) ||
			!DeleteIfPresent(PathScratchTwo))
			return false;

		for (unsigned rotation = RetainedRotations; rotation > 1; --rotation)
		{
			if (!MakeRotatedPath(rotation - 1, PathScratchOne, std::size(PathScratchOne)) ||
				!MakeRotatedPath(rotation, PathScratchTwo, std::size(PathScratchTwo)) ||
				!MoveIfPresent(PathScratchOne, PathScratchTwo))
				return false;
		}

		if (!MakeRotatedPath(1, PathScratchTwo, std::size(PathScratchTwo)))
			return false;
		return MoveIfPresent(LogFilePath, PathScratchTwo);
	}

	[[nodiscard]] HANDLE OpenLogForAppendLocked(const DWORD recordSize) noexcept
	{
		auto file = CreateFileW(
			LogFilePath,
			FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return INVALID_HANDLE_VALUE;

		LARGE_INTEGER size{};
		if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
		{
			CloseHandle(file);
			return INVALID_HANDLE_VALUE;
		}

		const auto currentSize = static_cast<std::uint64_t>(size.QuadPart);
		if (currentSize <= MaxLogFileBytes && recordSize <= MaxLogFileBytes - currentSize)
			return file;

		CloseHandle(file);
		if (!RotateLogLocked())
			return INVALID_HANDLE_VALUE;

		return CreateFileW(
			LogFilePath,
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
	}

	void FormatRecord(
		RecordBuilder& record,
		const Level level,
		const Log::EventId eventId,
		const HRESULT result,
		const std::uint64_t correlationId,
		const char* detail) noexcept
	{
		SYSTEMTIME utc{};
		GetSystemTime(&utc);

		char timestamp[32]{};
		_snprintf_s(timestamp, std::size(timestamp), _TRUNCATE,
			"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
			static_cast<unsigned>(utc.wYear),
			static_cast<unsigned>(utc.wMonth),
			static_cast<unsigned>(utc.wDay),
			static_cast<unsigned>(utc.wHour),
			static_cast<unsigned>(utc.wMinute),
			static_cast<unsigned>(utc.wSecond),
			static_cast<unsigned>(utc.wMilliseconds));
		record.Append(timestamp);
		record.Append(" level=");
		record.Append(LevelName(level));
		record.Append(" event=");
		record.Append(EventName(eventId));
		record.Append(" hr=");
		record.AppendHex32(static_cast<std::uint32_t>(result));
		record.Append(" correlation=");
		record.AppendDecimal(correlationId);
		record.Append(" pid=");
		record.AppendDecimal(GetCurrentProcessId());
		record.Append(" tid=");
		record.AppendDecimal(GetCurrentThreadId());
		record.Append(" arch=");
#if defined(_WIN64)
		record.Append("x64");
#else
		record.Append("x86");
#endif
		record.AppendQuotedDetail(detail);
		record.Finish();
	}

	void DebugFallback(const RecordBuilder& record) noexcept
	{
		OutputDebugStringA("LiveIcons: ");
		OutputDebugStringA(record.Data());
	}

	void Emit(
		const Level level,
		const Log::EventId eventId,
		const HRESULT result,
		const std::uint64_t correlationId,
		const char* detail) noexcept
	{
		const LastErrorGuard lastErrorGuard{};
#if !defined(_DEBUG)
		if (level == Level::Diagnostic)
			return;
#endif

		try
		{
			const LogLockGuard lock{};
			if (lock.IsRecursive())
				return;
			if (!lock.OwnsLock())
			{
				return;
			}

			const auto rateLimitDecision = ApplyRateLimitLocked(level);
			if (rateLimitDecision == RateLimitDecision::Suppress)
				return;

			RecordBuilder record{};
			if (rateLimitDecision == RateLimitDecision::WriteSuppressionMarker)
				FormatRecord(record, Level::Error, Log::EventId::Logger,
					HRESULT_FROM_WIN32(ERROR_TOO_MANY_CMDS), 0, "rate-limit-active");
			else
				FormatRecord(record, level, eventId, result, correlationId, detail);

			if (!InitializeLogPathLocked())
			{
				DebugFallback(record);
				return;
			}

			const auto file = OpenLogForAppendLocked(record.Size());
			if (file == INVALID_HANDLE_VALUE)
			{
				DebugFallback(record);
				return;
			}

			DWORD bytesWritten{};
			const auto written = WriteFile(file, record.Data(), record.Size(), &bytesWritten, nullptr);
			CloseHandle(file);
			if (!written || bytesWritten != record.Size())
				DebugFallback(record);
		}
		catch (...)
		{
			OutputDebugStringA("LiveIcons: logging failed without affecting the thumbnail request.\r\n");
		}
	}
}

namespace Log
{
	std::uint64_t NextCorrelationId() noexcept
	{
		const LastErrorGuard lastErrorGuard{};
		return static_cast<std::uint64_t>(InterlockedIncrement64(&CorrelationCounter));
	}

	void Diagnostic(
		const EventId eventId,
		const HRESULT result,
		const std::uint64_t correlationId,
		const char* detail) noexcept
	{
		Emit(Level::Diagnostic, eventId, result, correlationId, detail);
	}

	void Error(
		const EventId eventId,
		const HRESULT result,
		const std::uint64_t correlationId,
		const char* detail) noexcept
	{
		Emit(Level::Error, eventId, result, correlationId, detail);
	}

	void Exception(
		const EventId eventId,
		const HRESULT result,
		const std::uint64_t correlationId,
		const char* detail) noexcept
	{
		Emit(Level::Exception, eventId, result, correlationId, detail);
	}
}
