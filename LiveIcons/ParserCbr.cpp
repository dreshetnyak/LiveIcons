#include "pch.h"
#include "ParserCbr.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "CbrCoverSelection.h"
#include "Gfx.h"
#include "Utility.h"

// This integration deliberately uses only UnRAR's published DLL interface.
// UnRAR source code may be used in any software to handle RAR archives without
// limitations free of charge, but cannot be used to develop RAR (WinRAR)
// compatible archiver and to re-create RAR compression algorithm, which is
// proprietary. Distribution of modified UnRAR source code in separate form or
// as a part of other software is permitted, provided that full text of this
// paragraph, starting from "UnRAR source code" words, is included in license,
// or in documentation if license is not available, and in source code comments
// of resulting package. See ../unrar/license.txt for the complete license.
#include "../unrar/dll.hpp"

namespace
{
	constexpr std::uint64_t MaximumImageSize{ 64ULL * 1024ULL * 1024ULL };
	constexpr std::uint64_t MaximumArchiveSnapshotSize{ 1024ULL * 1024ULL * 1024ULL };
	constexpr std::uint64_t MaximumProcessedBytes{ 512ULL * 1024ULL * 1024ULL };
	constexpr std::uint64_t MaximumComicInfoDiscoveryWork{ 64ULL * 1024ULL * 1024ULL };
	constexpr std::uint64_t MaximumArchiveEntries{ 10000ULL };
	constexpr std::uint64_t MaximumComicInfoSize{ 1ULL * 1024ULL * 1024ULL };
	constexpr std::size_t MaximumCatalogNameCharacters{ 2U * 1024U * 1024U };
	// RARHeaderDataEx::DictSize is expressed in KiB. Keep the decoder's working
	// set suitable for Explorer's isolated thumbnail host.
	constexpr unsigned int MaximumDictionaryKilobytes{ 128U * 1024U };
	constexpr std::size_t StreamCopyBufferSize{ 1024U * 1024U };
	constexpr std::size_t LongArchiveNameCapacity{ 32768U };

	[[nodiscard]] std::mutex& GetUnrarMutex()
	{
		// The statically linked UnRAR implementation has process-global error state.
		static std::mutex mutex;
		return mutex;
	}

	class UniqueHandle final
	{
		HANDLE Handle{ INVALID_HANDLE_VALUE };

	public:
		UniqueHandle() = default;
		explicit UniqueHandle(const HANDLE handle) noexcept : Handle{ handle } { }
		UniqueHandle(const UniqueHandle&) = delete;
		UniqueHandle(UniqueHandle&&) = delete;
		UniqueHandle& operator=(const UniqueHandle&) = delete;
		UniqueHandle& operator=(UniqueHandle&&) = delete;

		~UniqueHandle() noexcept
		{
			if (Handle != INVALID_HANDLE_VALUE)
				CloseHandle(Handle);
		}

		[[nodiscard]] HANDLE Get() const noexcept { return Handle; }

		void Attach(const HANDLE handle) noexcept
		{
			if (Handle != INVALID_HANDLE_VALUE)
				CloseHandle(Handle);
			Handle = handle;
		}

		HRESULT Close() noexcept
		{
			if (Handle == INVALID_HANDLE_VALUE)
				return S_OK;

			const HANDLE handle = Handle;
			Handle = INVALID_HANDLE_VALUE;
			return CloseHandle(handle) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
		}
	};

	class UniqueBitmap final
	{
		HBITMAP Bitmap{};

	public:
		explicit UniqueBitmap(const HBITMAP bitmap = nullptr) noexcept : Bitmap{ bitmap } { }
		UniqueBitmap(const UniqueBitmap&) = delete;
		UniqueBitmap(UniqueBitmap&&) = delete;
		UniqueBitmap& operator=(const UniqueBitmap&) = delete;
		UniqueBitmap& operator=(UniqueBitmap&&) = delete;

		~UniqueBitmap() noexcept
		{
			if (Bitmap != nullptr)
				DeleteObject(Bitmap);
		}

		[[nodiscard]] HBITMAP Get() const noexcept { return Bitmap; }

		[[nodiscard]] HBITMAP Release() noexcept
		{
			return std::exchange(Bitmap, nullptr);
		}

		void Reset(const HBITMAP bitmap = nullptr) noexcept
		{
			if (Bitmap != bitmap && Bitmap != nullptr)
				DeleteObject(Bitmap);
			Bitmap = bitmap;
		}
	};

	class PendingTemporaryFile final
	{
		const wchar_t* Path;
		bool Armed{ true };

	public:
		explicit PendingTemporaryFile(const wchar_t* const path) noexcept : Path{ path } { }
		PendingTemporaryFile(const PendingTemporaryFile&) = delete;
		PendingTemporaryFile(PendingTemporaryFile&&) = delete;
		PendingTemporaryFile& operator=(const PendingTemporaryFile&) = delete;
		PendingTemporaryFile& operator=(PendingTemporaryFile&&) = delete;

		~PendingTemporaryFile() noexcept
		{
			if (Armed && Path != nullptr)
				DeleteFileW(Path);
		}

		void Release() noexcept { Armed = false; }
	};

	class TemporaryFile final
	{
		std::wstring Path;

	public:
		TemporaryFile() = default;
		TemporaryFile(const TemporaryFile&) = delete;
		TemporaryFile(TemporaryFile&&) = delete;
		TemporaryFile& operator=(const TemporaryFile&) = delete;
		TemporaryFile& operator=(TemporaryFile&&) = delete;

		~TemporaryFile() noexcept
		{
			// UnRAR reopens the snapshot by name without FILE_SHARE_DELETE, so a
			// delete-on-close handle cannot remain open. This owner removes it as
			// soon as UnRAR has closed the archive, including on error paths.
			if (!Path.empty())
				DeleteFileW(Path.c_str());
		}

		[[nodiscard]] const std::wstring& GetPath() const noexcept { return Path; }

		HRESULT Create(UniqueHandle& outHandle)
		{
			if (!Path.empty())
				return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);

			std::array<wchar_t, MAX_PATH + 1> temporaryDirectory{};
			const DWORD directoryLength = GetTempPathW(
				static_cast<DWORD>(temporaryDirectory.size()), temporaryDirectory.data());
			if (directoryLength == 0)
				return HRESULT_FROM_WIN32(GetLastError());
			if (directoryLength >= temporaryDirectory.size())
				return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

			std::array<wchar_t, MAX_PATH + 1> temporaryName{};
			if (GetTempFileNameW(temporaryDirectory.data(), L"LIC", 0, temporaryName.data()) == 0)
				return HRESULT_FROM_WIN32(GetLastError());

			// GetTempFileNameW creates the file. Keep a non-allocating owner armed
			// until the durable path owner has successfully copied its name.
			PendingTemporaryFile pendingFile{ temporaryName.data() };
			Path.assign(temporaryName.data());
			pendingFile.Release();
			const HANDLE handle = CreateFileW(
				Path.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
				FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
				return HRESULT_FROM_WIN32(GetLastError());

			outHandle.Attach(handle);
			return S_OK;
		}
	};

	class StreamPositionRestorer final
	{
		IStream* Stream;
		ULARGE_INTEGER OriginalPosition{};
		HRESULT CaptureResult;
		bool Restored{ false };

	public:
		explicit StreamPositionRestorer(IStream* const stream) noexcept :
			Stream{ stream }, CaptureResult{ E_POINTER }
		{
			if (Stream == nullptr)
				return;

			try
			{
				constexpr LARGE_INTEGER zero{};
				CaptureResult = Stream->Seek(zero, STREAM_SEEK_CUR, &OriginalPosition);
				if (SUCCEEDED(CaptureResult) &&
					OriginalPosition.QuadPart > static_cast<ULONGLONG>((std::numeric_limits<LONGLONG>::max)()))
				{
					CaptureResult = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
				}
			}
			catch (const std::bad_alloc&)
			{
				CaptureResult = E_OUTOFMEMORY;
			}
			catch (...)
			{
				CaptureResult = E_FAIL;
			}
		}

		StreamPositionRestorer(const StreamPositionRestorer&) = delete;
		StreamPositionRestorer(StreamPositionRestorer&&) = delete;
		StreamPositionRestorer& operator=(const StreamPositionRestorer&) = delete;
		StreamPositionRestorer& operator=(StreamPositionRestorer&&) = delete;

		~StreamPositionRestorer() noexcept
		{
			static_cast<void>(Restore());
		}

		[[nodiscard]] HRESULT GetCaptureResult() const noexcept { return CaptureResult; }

		HRESULT Rewind() const noexcept
		{
			if (FAILED(CaptureResult))
				return CaptureResult;
			try
			{
				constexpr LARGE_INTEGER zero{};
				return Stream->Seek(zero, STREAM_SEEK_SET, nullptr);
			}
			catch (const std::bad_alloc&)
			{
				return E_OUTOFMEMORY;
			}
			catch (...)
			{
				return E_FAIL;
			}
		}

		HRESULT Restore() noexcept
		{
			if (Restored || FAILED(CaptureResult))
				return CaptureResult;

			try
			{
				LARGE_INTEGER position{};
				position.QuadPart = static_cast<LONGLONG>(OriginalPosition.QuadPart);
				const HRESULT result = Stream->Seek(position, STREAM_SEEK_SET, nullptr);
				if (SUCCEEDED(result))
					Restored = true;
				return result;
			}
			catch (const std::bad_alloc&)
			{
				return E_OUTOFMEMORY;
			}
			catch (...)
			{
				return E_FAIL;
			}
		}
	};

	class RarArchive final
	{
		HANDLE Handle{ nullptr };

	public:
		RarArchive() = default;
		explicit RarArchive(const HANDLE handle) noexcept : Handle{ handle } { }
		RarArchive(const RarArchive&) = delete;
		RarArchive(RarArchive&&) = delete;
		RarArchive& operator=(const RarArchive&) = delete;
		RarArchive& operator=(RarArchive&&) = delete;

		~RarArchive() noexcept
		{
			if (Handle != nullptr)
			{
				try
				{
					RARCloseArchive(Handle);
				}
				catch (...)
				{
					// A destructor cannot report an UnRAR implementation failure.
				}
			}
		}

		[[nodiscard]] HANDLE Get() const noexcept { return Handle; }

		void Attach(const HANDLE handle) noexcept
		{
			if (Handle == nullptr)
				Handle = handle;
		}
	};

	struct WorkBudget final
	{
		std::uint64_t DeclaredBytes{};
		std::uint64_t ActualBytes{};

		[[nodiscard]] bool ReserveDeclared(const std::uint64_t byteCount) noexcept
		{
			if (byteCount > MaximumProcessedBytes ||
				DeclaredBytes > MaximumProcessedBytes - byteCount)
				return false;
			DeclaredBytes += byteCount;
			return true;
		}

		[[nodiscard]] bool RecordActual(const std::uint64_t byteCount) noexcept
		{
			if (byteCount > MaximumProcessedBytes ||
				ActualBytes > MaximumProcessedBytes - byteCount)
				return false;
			ActualBytes += byteCount;
			return true;
		}

		[[nodiscard]] std::uint64_t RemainingDeclared() const noexcept
		{
			return MaximumProcessedBytes - DeclaredBytes;
		}
	};

	struct CallbackContext final
	{
		WorkBudget& Budget;
		std::vector<char>* Output{};
		std::size_t CaptureLimit{};
		bool CaptureData{};
		bool AllocationFailed{};
		bool PasswordRequested{};
		bool MissingVolume{};
		bool LargeDictionary{};
		bool ProcessingLimitExceeded{};
		bool UnexpectedFailure{};

		explicit CallbackContext(WorkBudget& budget) noexcept :
			Budget{ budget }
		{
		}

		void Reset(
			std::vector<char>* const output = nullptr,
			const std::size_t captureLimit = 0) noexcept
		{
			Output = output;
			CaptureLimit = captureLimit;
			CaptureData = output != nullptr;
			AllocationFailed = false;
			PasswordRequested = false;
			MissingVolume = false;
			LargeDictionary = false;
			ProcessingLimitExceeded = false;
			UnexpectedFailure = false;
		}
	};

	int ProcessUnrarCallback(
		CallbackContext& context, const UINT message, const LPARAM parameter1,
		const LPARAM parameter2)
	{
		switch (message)
		{
		case UCM_PROCESSDATA:
		{
			if (parameter2 < 0 || (parameter1 == 0 && parameter2 != 0))
			{
				context.UnexpectedFailure = true;
				return -1;
			}

			const auto byteCount = static_cast<std::size_t>(parameter2);
			if (!context.Budget.RecordActual(byteCount))
			{
				context.ProcessingLimitExceeded = true;
				return -1;
			}

			if (!context.CaptureData || context.Output == nullptr || byteCount == 0)
				return 0;

			if (context.Output->size() > context.CaptureLimit)
			{
				context.UnexpectedFailure = true;
				return -1;
			}
			const std::size_t remaining = context.CaptureLimit - context.Output->size();
			if (byteCount > remaining)
			{
				// RARProcessFile can emit associated service-record data after the
				// selected entry. Ignore only callbacks that begin after the declared
				// entry size has been captured; an entry that overruns its own declared
				// size is malformed.
				if (remaining != 0)
				{
					context.UnexpectedFailure = true;
					return -1;
				}
				return 0;
			}
			if (byteCount == 0)
				return 0;

			const auto* const bytes = reinterpret_cast<const char*>(parameter1);
			try
			{
				context.Output->insert(
					context.Output->end(), bytes, bytes + byteCount);
			}
			catch (const std::bad_alloc&)
			{
				context.AllocationFailed = true;
				return -1;
			}
			catch (const std::length_error&)
			{
				context.AllocationFailed = true;
				return -1;
			}
			return 0;
		}

		case UCM_NEEDPASSWORD:
		case UCM_NEEDPASSWORDW:
			context.PasswordRequested = true;
			return -1;

		case UCM_CHANGEVOLUME:
		case UCM_CHANGEVOLUMEW:
			if (parameter2 == RAR_VOL_ASK)
			{
				context.MissingVolume = true;
				return -1;
			}
			return 0;

		case UCM_LARGEDICT:
			context.LargeDictionary = true;
			return 0; // UCM_LARGEDICT requires 1, rather than 0, to permit extraction.

		default:
			return 0;
		}
	}

	int CALLBACK UnrarCallback(
		const UINT message, const LPARAM userData, const LPARAM parameter1,
		const LPARAM parameter2) noexcept
	{
		auto* const context = reinterpret_cast<CallbackContext*>(userData);
		if (context == nullptr)
			return -1;

		try
		{
			return ProcessUnrarCallback(*context, message, parameter1, parameter2);
		}
		catch (const std::bad_alloc&)
		{
			context->AllocationFailed = true;
			return -1;
		}
		catch (const std::length_error&)
		{
			context->AllocationFailed = true;
			return -1;
		}
		catch (...)
		{
			context->UnexpectedFailure = true;
			return -1;
		}
	}

	[[nodiscard]] bool EqualsAsciiCaseInsensitive(
		const std::wstring_view left, const std::wstring_view right) noexcept
	{
		if (left.size() != right.size())
			return false;

		for (std::size_t index = 0; index < left.size(); ++index)
		{
			wchar_t leftCharacter = left[index];
			wchar_t rightCharacter = right[index];
			if (leftCharacter >= L'A' && leftCharacter <= L'Z')
				leftCharacter += L'a' - L'A';
			if (rightCharacter >= L'A' && rightCharacter <= L'Z')
				rightCharacter += L'a' - L'A';
			if (leftCharacter != rightCharacter)
				return false;
		}
		return true;
	}

	[[nodiscard]] bool IsImageFileName(const std::wstring_view fileName) noexcept
	{
		static constexpr std::array<std::wstring_view, 20> imageExtensions
		{
			L".bmp", L".dib", L".gif", L".heic", L".heif",
			L".ico", L".jfif", L".jpe", L".jpeg", L".jpg",
			L".jxr", L".png", L".rle", L".tif", L".tiff",
			L".wdp", L".webp", L".avif", L".jp2", L".j2k"
		};

		const std::size_t separatorOffset = fileName.find_last_of(L"/\\");
		const std::size_t extensionOffset = fileName.find_last_of(L'.');
		if (extensionOffset == std::wstring_view::npos ||
			separatorOffset != std::wstring_view::npos && extensionOffset < separatorOffset)
			return false;

		const std::wstring_view extension = fileName.substr(extensionOffset);
		for (const auto candidate : imageExtensions)
			if (EqualsAsciiCaseInsensitive(extension, candidate))
				return true;
		return false;
	}

	[[nodiscard]] std::uint64_t GetUncompressedSize(const RARHeaderDataEx& header) noexcept
	{
		return static_cast<std::uint64_t>(header.UnpSize) |
			static_cast<std::uint64_t>(header.UnpSizeHigh) << 32U;
	}

	[[nodiscard]] std::wstring GetHeaderFileName(
		const RARHeaderDataEx& header, const std::vector<wchar_t>& longName)
	{
		if (!longName.empty() && longName.front() != L'\0')
		{
			const auto terminator = std::find(longName.begin(), longName.end(), L'\0');
			return std::wstring{ longName.begin(), terminator };
		}

		const auto fileNameEnd = std::find(std::begin(header.FileNameW), std::end(header.FileNameW), L'\0');
		return std::wstring{ std::begin(header.FileNameW), fileNameEnd };
	}

	[[nodiscard]] bool StartsWithAsciiCaseInsensitive(
		const std::wstring_view value, const std::wstring_view prefix) noexcept
	{
		return value.size() >= prefix.size() &&
			EqualsAsciiCaseInsensitive(value.substr(0, prefix.size()), prefix);
	}

	[[nodiscard]] bool IsMacOsSidecarPath(const std::wstring_view fileName) noexcept
	{
		const std::size_t firstSeparator = fileName.find_first_of(L"/\\");
		if (EqualsAsciiCaseInsensitive(
			fileName.substr(0, firstSeparator), L"__MACOSX"))
			return true;

		const std::size_t lastSeparator = fileName.find_last_of(L"/\\");
		const std::wstring_view baseName = lastSeparator == std::wstring_view::npos
			? fileName
			: fileName.substr(lastSeparator + 1);
		return StartsWithAsciiCaseInsensitive(baseName, L"._");
	}

	[[nodiscard]] bool IsRootComicInfoPath(const std::wstring_view fileName) noexcept
	{
		return fileName.find_first_of(L"/\\") == std::wstring_view::npos &&
			EqualsAsciiCaseInsensitive(fileName, L"ComicInfo.xml");
	}

	constexpr std::size_t NoIndex{ (std::numeric_limits<std::size_t>::max)() };

	struct EntryRecord final
	{
		std::uint64_t UncompressedSize{};
		unsigned int Flags{};
		unsigned int DictionaryKilobytes{};
		unsigned int FileCrc{};
		unsigned int HashType{};
		std::array<char, 32> Hash{};
		unsigned int RedirectionType{};
	};

	struct ImageCandidate final
	{
		std::size_t ArchiveIndex{};
		std::wstring Name;
		std::size_t NaturalIndex{};
		std::size_t Rank{};
	};

	struct ArchiveCatalog final
	{
		bool Solid{};
		bool FoundEncryptedImage{};
		bool FoundOversizedImage{};
		bool FoundExcessiveDictionaryImage{};
		bool ComicInfoAmbiguous{};
		std::size_t StoredNameCharacters{};
		std::optional<std::size_t> ComicInfoArchiveIndex;
		std::vector<EntryRecord> Entries;
		std::vector<ImageCandidate> Images;
	};

	[[nodiscard]] EntryRecord MakeEntryRecord(const RARHeaderDataEx& header) noexcept
	{
		EntryRecord record{};
		record.UncompressedSize = GetUncompressedSize(header);
		record.Flags = header.Flags;
		record.DictionaryKilobytes = header.DictSize;
		record.FileCrc = header.FileCRC;
		record.HashType = header.HashType;
		std::copy(std::begin(header.Hash), std::end(header.Hash), record.Hash.begin());
		record.RedirectionType = header.RedirType;
		return record;
	}

	[[nodiscard]] bool HeaderMatchesRecord(
		const RARHeaderDataEx& header, const EntryRecord& record) noexcept
	{
		return GetUncompressedSize(header) == record.UncompressedSize &&
			header.Flags == record.Flags &&
			header.DictSize == record.DictionaryKilobytes &&
			header.FileCRC == record.FileCrc &&
			header.HashType == record.HashType &&
			header.RedirType == record.RedirectionType &&
			std::equal(std::begin(header.Hash), std::end(header.Hash), record.Hash.begin());
	}

	[[nodiscard]] bool IsStaticallyUsableImage(const EntryRecord& entry) noexcept
	{
		return (entry.Flags & RHDF_ENCRYPTED) == 0 &&
			entry.UncompressedSize != 0 && entry.UncompressedSize <= MaximumImageSize &&
			entry.UncompressedSize <= (std::numeric_limits<std::size_t>::max)() &&
			entry.DictionaryKilobytes <= MaximumDictionaryKilobytes;
	}

	[[nodiscard]] bool EntryRecordLess(
		const EntryRecord& left, const EntryRecord& right) noexcept
	{
		if (left.UncompressedSize != right.UncompressedSize)
			return left.UncompressedSize < right.UncompressedSize;
		if (left.Flags != right.Flags)
			return left.Flags < right.Flags;
		if (left.DictionaryKilobytes != right.DictionaryKilobytes)
			return left.DictionaryKilobytes < right.DictionaryKilobytes;
		if (left.FileCrc != right.FileCrc)
			return left.FileCrc < right.FileCrc;
		if (left.HashType != right.HashType)
			return left.HashType < right.HashType;
		if (left.Hash != right.Hash)
			return left.Hash < right.Hash;
		return left.RedirectionType < right.RedirectionType;
	}

	[[nodiscard]] HRESULT RarErrorToHResult(const int error) noexcept
	{
		switch (error)
		{
		case ERAR_SUCCESS: return S_OK;
		case ERAR_NO_MEMORY: return E_OUTOFMEMORY;
		case ERAR_BAD_DATA: return HRESULT_FROM_WIN32(ERROR_CRC);
		case ERAR_BAD_ARCHIVE:
		case ERAR_UNKNOWN_FORMAT: return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
		case ERAR_EOPEN: return HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
		case ERAR_ECREATE: return HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE);
		case ERAR_ECLOSE: return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
		case ERAR_EREAD: return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
		case ERAR_EWRITE: return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
		case ERAR_SMALL_BUF: return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
		case ERAR_MISSING_PASSWORD:
		case ERAR_BAD_PASSWORD: return HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD);
		case ERAR_EREFERENCE:
		case ERAR_LARGE_DICT: return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
		default: return E_FAIL;
		}
	}

	[[nodiscard]] std::wstring_view RarErrorDescription(const int error) noexcept
	{
		switch (error)
		{
		case ERAR_NO_MEMORY: return L"UnRAR ran out of memory";
		case ERAR_BAD_DATA: return L"the archive contains damaged data";
		case ERAR_BAD_ARCHIVE: return L"the file is not a valid RAR archive";
		case ERAR_UNKNOWN_FORMAT: return L"the RAR format is not recognized";
		case ERAR_EOPEN: return L"the archive could not be opened";
		case ERAR_ECREATE: return L"a destination could not be created";
		case ERAR_ECLOSE: return L"an archive handle could not be closed";
		case ERAR_EREAD: return L"the archive could not be read";
		case ERAR_EWRITE: return L"UnRAR reported a write error while testing an entry";
		case ERAR_SMALL_BUF: return L"an UnRAR API buffer was too small";
		case ERAR_MISSING_PASSWORD: return L"the archive requires a password";
		case ERAR_BAD_PASSWORD: return L"the archive password is invalid";
		case ERAR_EREFERENCE: return L"the archive contains an unsupported reference";
		case ERAR_LARGE_DICT: return L"the archive needs a dictionary larger than the configured limit";
		default: return L"UnRAR reported an unknown error";
		}
	}

	[[nodiscard]] Parser::Result MakeRarError(
		const std::wstring_view operation, const int error)
	{
		return Parser::Result{
			error == ERAR_SUCCESS ? E_FAIL : RarErrorToHResult(error),
			std::format(L"{}: {} (UnRAR error {}).", operation, RarErrorDescription(error), error)
		};
	}

	[[nodiscard]] Parser::Result MakeEncryptedError()
	{
		return Parser::Result{
			HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD),
			L"The CBR archive or an image entry is encrypted; LiveIcons does not request or store archive passwords."
		};
	}

	[[nodiscard]] Parser::Result MakeCallbackError(const CallbackContext& callback)
	{
		if (callback.PasswordRequested)
			return MakeEncryptedError();
		if (callback.ProcessingLimitExceeded)
			return Parser::Result{
				HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
				L"CBR processing exceeded LiveIcons' 512 MiB decompression-work safety limit."
			};
		if (callback.AllocationFailed)
			return Parser::Result{ E_OUTOFMEMORY, L"Not enough memory was available to read a CBR image entry." };
		if (callback.MissingVolume)
			return Parser::Result{
				HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
				L"The CBR is a multi-volume archive and a required volume is missing."
			};
		if (callback.LargeDictionary)
			return Parser::Result{
				HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),
				L"The CBR requires a RAR dictionary larger than UnRAR permits for this operation."
			};
		if (callback.UnexpectedFailure)
			return Parser::Result{ E_FAIL, L"An unexpected failure occurred in the UnRAR data callback." };
		return Parser::Result{ E_FAIL, L"UnRAR stopped while processing a CBR entry." };
	}

	[[nodiscard]] bool HasCallbackError(const CallbackContext& callback) noexcept
	{
		return callback.PasswordRequested || callback.AllocationFailed ||
			callback.MissingVolume || callback.LargeDictionary || callback.ProcessingLimitExceeded ||
			callback.UnexpectedFailure;
	}

	[[nodiscard]] Parser::Result OpenRarArchive(
		const std::wstring& fileName,
		const unsigned int openMode,
		CallbackContext& callback,
		std::wstring& mutableFileName,
		RAROpenArchiveDataEx& openData,
		RarArchive& outArchive)
	{
		mutableFileName = fileName;
		openData = {};
		openData.ArcNameW = mutableFileName.data();
		openData.OpenMode = openMode;
		openData.Callback = UnrarCallback;
		openData.UserData = reinterpret_cast<LPARAM>(&callback);

		const HANDLE rawArchive = RAROpenArchiveEx(&openData);
		if (rawArchive == nullptr)
		{
			if (HasCallbackError(callback))
				return MakeCallbackError(callback);
			if (openData.OpenResult == ERAR_MISSING_PASSWORD ||
				openData.OpenResult == ERAR_BAD_PASSWORD)
				return MakeEncryptedError();
			return MakeRarError(L"Unable to open the CBR archive", openData.OpenResult);
		}
		outArchive.Attach(rawArchive);

		if ((openData.Flags & ROADF_VOLUME) != 0)
			return Parser::Result{
				HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),
				L"Multi-volume CBR archives are not supported because thumbnail extraction must remain confined to one input file."
			};
		if ((openData.Flags & ROADF_ENCHEADERS) != 0)
			return MakeEncryptedError();
		return Parser::Result{ S_OK };
	}

	void AssignCandidateRanks(
		ArchiveCatalog& catalog,
		const std::vector<std::size_t>& frontCoverNaturalIndices)
	{
		std::vector<std::size_t> frontCoverOrder(catalog.Images.size(), NoIndex);
		for (std::size_t order{}; order < frontCoverNaturalIndices.size(); ++order)
		{
			const std::size_t naturalIndex = frontCoverNaturalIndices[order];
			if (naturalIndex < frontCoverOrder.size() &&
				frontCoverOrder[naturalIndex] == NoIndex)
				frontCoverOrder[naturalIndex] = order;
		}

		std::ranges::sort(catalog.Images, [&frontCoverOrder](
			const ImageCandidate& left, const ImageCandidate& right)
		{
			const std::size_t leftFrontOrder = frontCoverOrder[left.NaturalIndex];
			const std::size_t rightFrontOrder = frontCoverOrder[right.NaturalIndex];
			if ((leftFrontOrder != NoIndex) != (rightFrontOrder != NoIndex))
				return leftFrontOrder != NoIndex;
			if (leftFrontOrder != rightFrontOrder && leftFrontOrder != NoIndex)
				return leftFrontOrder < rightFrontOrder;

			const bool leftExplicit = CbrCoverSelection::IsExplicitCoverName(left.Name);
			const bool rightExplicit = CbrCoverSelection::IsExplicitCoverName(right.Name);
			if (leftExplicit != rightExplicit)
				return leftExplicit;
			return left.NaturalIndex < right.NaturalIndex;
		});

		for (std::size_t rank{}; rank < catalog.Images.size(); ++rank)
			catalog.Images[rank].Rank = rank;
	}

	[[nodiscard]] Parser::Result CatalogArchive(
		const std::wstring& fileName,
		CallbackContext& callback,
		ArchiveCatalog& outCatalog)
	{
		callback.Reset();
		std::wstring mutableFileName;
		RAROpenArchiveDataEx openData{};
		RarArchive archive;
		Parser::Result result = OpenRarArchive(
			fileName, RAR_OM_LIST, callback, mutableFileName, openData, archive);
		if (FAILED(result.HResult))
			return result;

		outCatalog.Solid = (openData.Flags & ROADF_SOLID) != 0;
		std::vector<wchar_t> longFileName(LongArchiveNameCapacity);
		for (;;)
		{
			RARHeaderDataEx header{};
			longFileName.front() = L'\0';
			header.FileNameEx = longFileName.data();
			header.FileNameExSize = static_cast<unsigned int>(longFileName.size());

			const int headerResult = RARReadHeaderEx(archive.Get(), &header);
			if (headerResult == ERAR_END_ARCHIVE)
				break;
			if (headerResult == ERAR_MISSING_PASSWORD || headerResult == ERAR_BAD_PASSWORD)
				return MakeEncryptedError();
			if (headerResult != ERAR_SUCCESS)
				return MakeRarError(L"Unable to enumerate the CBR archive", headerResult);
			if (outCatalog.Entries.size() >= MaximumArchiveEntries)
				return Parser::Result{
					HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
					L"The CBR contains more than LiveIcons' 10,000-entry safety limit."
				};

			const std::size_t archiveIndex = outCatalog.Entries.size();
			const EntryRecord record = MakeEntryRecord(header);
			outCatalog.Entries.push_back(record);
			const std::wstring entryName = GetHeaderFileName(header, longFileName);
			const bool isDirectory = (header.Flags & RHDF_DIRECTORY) != 0;
			const bool isRedirect = header.RedirType != 0;
			const bool isImage = !isDirectory && !isRedirect &&
				!IsMacOsSidecarPath(entryName) && IsImageFileName(entryName);

			if (isImage)
			{
				if (entryName.size() > MaximumCatalogNameCharacters -
					outCatalog.StoredNameCharacters)
					return Parser::Result{
						HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
						L"CBR image filenames exceed LiveIcons' catalog memory limit."
					};
				outCatalog.StoredNameCharacters += entryName.size();
				outCatalog.FoundEncryptedImage = outCatalog.FoundEncryptedImage ||
					(header.Flags & RHDF_ENCRYPTED) != 0;
				outCatalog.FoundOversizedImage = outCatalog.FoundOversizedImage ||
					record.UncompressedSize > MaximumImageSize;
				outCatalog.FoundExcessiveDictionaryImage =
					outCatalog.FoundExcessiveDictionaryImage ||
					record.DictionaryKilobytes > MaximumDictionaryKilobytes;
				outCatalog.Images.push_back(ImageCandidate{ archiveIndex, entryName });
			}
			else if (!isDirectory && !isRedirect && IsRootComicInfoPath(entryName))
			{
				if (outCatalog.ComicInfoArchiveIndex.has_value())
				{
					outCatalog.ComicInfoArchiveIndex.reset();
					outCatalog.ComicInfoAmbiguous = true;
				}
				else if (!outCatalog.ComicInfoAmbiguous)
				{
					outCatalog.ComicInfoArchiveIndex = archiveIndex;
				}
			}

			const int processResult = RARProcessFileW(
				archive.Get(), RAR_SKIP, nullptr, nullptr);
			if (HasCallbackError(callback))
				return MakeCallbackError(callback);
			if (processResult == ERAR_MISSING_PASSWORD || processResult == ERAR_BAD_PASSWORD)
				return MakeEncryptedError();
			if (processResult != ERAR_SUCCESS)
				return MakeRarError(
					std::format(L"Unable to catalog CBR entry '{}'", entryName), processResult);
		}

		std::ranges::sort(outCatalog.Images, [&outCatalog](
			const ImageCandidate& left, const ImageCandidate& right)
		{
			if (CbrCoverSelection::NaturalLess(left.Name, right.Name))
				return true;
			if (CbrCoverSelection::NaturalLess(right.Name, left.Name))
				return false;
			if (left.Name != right.Name)
				return left.Name < right.Name;
			const EntryRecord& leftEntry = outCatalog.Entries[left.ArchiveIndex];
			const EntryRecord& rightEntry = outCatalog.Entries[right.ArchiveIndex];
			if (EntryRecordLess(leftEntry, rightEntry))
				return true;
			if (EntryRecordLess(rightEntry, leftEntry))
				return false;
			return left.ArchiveIndex < right.ArchiveIndex;
		});
		for (std::size_t index{}; index < outCatalog.Images.size(); ++index)
			outCatalog.Images[index].NaturalIndex = index;
		AssignCandidateRanks(outCatalog, {});
		return Parser::Result{ S_OK };
	}

	[[nodiscard]] std::uint64_t EstimateEntryReadWork(
		const ArchiveCatalog& catalog, const std::size_t targetArchiveIndex) noexcept
	{
		if (targetArchiveIndex >= catalog.Entries.size())
			return MaximumProcessedBytes + 1;
		if (!catalog.Solid)
			return catalog.Entries[targetArchiveIndex].UncompressedSize;

		std::uint64_t total{};
		for (std::size_t index{}; index <= targetArchiveIndex; ++index)
		{
			const std::uint64_t size = catalog.Entries[index].UncompressedSize;
			if (size > MaximumProcessedBytes || total > MaximumProcessedBytes - size)
				return MaximumProcessedBytes + 1;
			total += size;
		}
		return total;
	}

	[[nodiscard]] bool ShouldReadComicInfo(const ArchiveCatalog& catalog) noexcept
	{
		if (!catalog.ComicInfoArchiveIndex.has_value() || catalog.ComicInfoAmbiguous)
			return false;
		const std::size_t comicInfoIndex = *catalog.ComicInfoArchiveIndex;
		if (comicInfoIndex >= catalog.Entries.size())
			return false;
		const EntryRecord& comicInfo = catalog.Entries[comicInfoIndex];
		if (comicInfo.UncompressedSize == 0 ||
			comicInfo.UncompressedSize > MaximumComicInfoSize ||
			(comicInfo.Flags & RHDF_ENCRYPTED) != 0 ||
			comicInfo.DictionaryKilobytes > MaximumDictionaryKilobytes)
			return false;

		const auto fallback = std::ranges::find_if(catalog.Images, [&catalog](
			const ImageCandidate& candidate)
		{
			return IsStaticallyUsableImage(
				catalog.Entries[candidate.ArchiveIndex]);
		});
		if (fallback == catalog.Images.end())
			return false;

		const std::uint64_t metadataWork = EstimateEntryReadWork(catalog, comicInfoIndex);
		const std::uint64_t fallbackWork = EstimateEntryReadWork(catalog, fallback->ArchiveIndex);
		return metadataWork <= MaximumComicInfoDiscoveryWork &&
			fallbackWork <= MaximumProcessedBytes &&
			metadataWork <= MaximumProcessedBytes - fallbackWork;
	}

	[[nodiscard]] Parser::Result ReadCatalogEntry(
		const std::wstring& fileName,
		const ArchiveCatalog& catalog,
		const std::size_t targetArchiveIndex,
		const std::size_t maximumCaptureSize,
		WorkBudget& budget,
		std::vector<char>& outContent)
	{
		outContent.clear();
		if (targetArchiveIndex >= catalog.Entries.size())
			return Parser::Result{ E_INVALIDARG, L"The requested CBR entry is outside the archive catalog." };

		CallbackContext callback{ budget };
		callback.Reset();
		std::wstring mutableFileName;
		RAROpenArchiveDataEx openData{};
		RarArchive archive;
		Parser::Result result = OpenRarArchive(
			fileName, RAR_OM_EXTRACT, callback, mutableFileName, openData, archive);
		if (FAILED(result.HResult))
			return result;
		if (((openData.Flags & ROADF_SOLID) != 0) != catalog.Solid)
			return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
				L"The CBR changed while LiveIcons was reading its metadata." };

		std::vector<wchar_t> longFileName(LongArchiveNameCapacity);
		for (std::size_t archiveIndex{}; archiveIndex <= targetArchiveIndex; ++archiveIndex)
		{
			RARHeaderDataEx header{};
			longFileName.front() = L'\0';
			header.FileNameEx = longFileName.data();
			header.FileNameExSize = static_cast<unsigned int>(longFileName.size());
			const int headerResult = RARReadHeaderEx(archive.Get(), &header);
			if (headerResult != ERAR_SUCCESS)
				return MakeRarError(L"Unable to reread the CBR archive", headerResult);
			if (!HeaderMatchesRecord(header, catalog.Entries[archiveIndex]))
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
					L"The CBR changed while LiveIcons was selecting its cover." };

			const bool capture = archiveIndex == targetArchiveIndex;
			if (capture && !IsRootComicInfoPath(GetHeaderFileName(header, longFileName)))
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
					L"The CBR metadata name changed while LiveIcons was selecting its cover." };
			const std::uint64_t uncompressedSize = GetUncompressedSize(header);
			if (capture && (uncompressedSize > maximumCaptureSize ||
				uncompressedSize > (std::numeric_limits<std::size_t>::max)()))
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
					L"The selected CBR metadata entry exceeds its capture limit." };

			if (capture)
			{
				try
				{
					outContent.reserve(static_cast<std::size_t>(uncompressedSize));
				}
				catch (const std::bad_alloc&)
				{
					return Parser::Result{ E_OUTOFMEMORY };
				}
				callback.Reset(&outContent, static_cast<std::size_t>(uncompressedSize));
			}
			else
			{
				callback.Reset();
			}

			const bool testEntry = capture || catalog.Solid ||
				(header.Flags & RHDF_SOLID) != 0;
			if (testEntry && header.DictSize > MaximumDictionaryKilobytes)
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),
					L"The CBR requires a RAR dictionary larger than LiveIcons' 128 MiB safety limit." };
			if (testEntry && !budget.ReserveDeclared(uncompressedSize))
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
					L"CBR processing would exceed LiveIcons' 512 MiB decompression-work safety limit." };

			const int processResult = RARProcessFileW(
				archive.Get(), testEntry ? RAR_TEST : RAR_SKIP, nullptr, nullptr);
			if (HasCallbackError(callback))
				return MakeCallbackError(callback);
			if (processResult == ERAR_MISSING_PASSWORD || processResult == ERAR_BAD_PASSWORD)
				return MakeEncryptedError();
			if (processResult != ERAR_SUCCESS)
				return MakeRarError(L"Unable to read CBR metadata", processResult);

			if (capture)
			{
				if (outContent.size() != static_cast<std::size_t>(uncompressedSize))
					return Parser::Result{ HRESULT_FROM_WIN32(ERROR_READ_FAULT),
						L"UnRAR returned an incomplete CBR metadata entry." };
				return Parser::Result{ S_OK };
			}
		}
		return Parser::Result{ HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
			L"The selected CBR metadata entry was not found." };
	}

	[[nodiscard]] Parser::Result TakeCoverResult(
		UniqueBitmap& bitmap, const WTS_ALPHATYPE alphaType)
	{
		Parser::Result result{ std::wstring{}, bitmap.Get(), alphaType };
		static_cast<void>(bitmap.Release());
		return result;
	}

	[[nodiscard]] Parser::Result ExtractRankedCover(
		const std::wstring& fileName,
		const ArchiveCatalog& catalog,
		WorkBudget& budget)
	{
		if (catalog.Images.empty())
			return Parser::Result{ HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
				L"The CBR archive does not contain a supported image entry." };

		std::vector<std::size_t> candidateByArchiveIndex(
			catalog.Entries.size(), NoIndex);
		for (std::size_t candidateIndex{}; candidateIndex < catalog.Images.size(); ++candidateIndex)
		{
			const std::size_t archiveIndex = catalog.Images[candidateIndex].ArchiveIndex;
			if (archiveIndex >= candidateByArchiveIndex.size() ||
				candidateByArchiveIndex[archiveIndex] != NoIndex)
				return Parser::Result{ E_UNEXPECTED,
					L"The CBR image catalog contains an invalid archive index." };
			candidateByArchiveIndex[archiveIndex] = candidateIndex;
		}

		// Non-solid entries are independently decodable. Select the bounded
		// attempt set in logical-rank order before walking physical headers so
		// archive storage order cannot consume the work budget on worse pages.
		std::vector<unsigned char> attemptCandidate(catalog.Images.size());
		bool excludedCandidateByWorkBudget{};
		std::uint64_t remainingDeclared = budget.RemainingDeclared();
		for (std::size_t candidateIndex{}; candidateIndex < catalog.Images.size(); ++candidateIndex)
		{
			const ImageCandidate& candidate = catalog.Images[candidateIndex];
			const EntryRecord& entry = catalog.Entries[candidate.ArchiveIndex];
			if (!IsStaticallyUsableImage(entry))
				continue;
			if (catalog.Solid)
			{
				attemptCandidate[candidateIndex] = 1;
				continue;
			}
			if (entry.UncompressedSize <= remainingDeclared)
			{
				attemptCandidate[candidateIndex] = 1;
				remainingDeclared -= entry.UncompressedSize;
			}
			else
			{
				excludedCandidateByWorkBudget = true;
			}
		}

		std::vector<std::size_t> bestRemainingRank(
			catalog.Entries.size() + 1, NoIndex);
		for (std::size_t archiveIndex = catalog.Entries.size(); archiveIndex-- > 0;)
		{
			bestRemainingRank[archiveIndex] = bestRemainingRank[archiveIndex + 1];
			const std::size_t candidateIndex = candidateByArchiveIndex[archiveIndex];
			if (candidateIndex != NoIndex && attemptCandidate[candidateIndex] != 0)
				bestRemainingRank[archiveIndex] = (std::min)(
					bestRemainingRank[archiveIndex], catalog.Images[candidateIndex].Rank);
		}

		CallbackContext callback{ budget };
		callback.Reset();
		std::wstring mutableFileName;
		RAROpenArchiveDataEx openData{};
		RarArchive archive;
		Parser::Result openResult = OpenRarArchive(
			fileName, RAR_OM_EXTRACT, callback, mutableFileName, openData, archive);
		if (FAILED(openResult.HResult))
			return openResult;
		if (((openData.Flags & ROADF_SOLID) != 0) != catalog.Solid)
			return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
				L"The CBR changed while LiveIcons was selecting its cover." };

		UniqueBitmap bestBitmap;
		WTS_ALPHATYPE bestAlpha{ WTSAT_UNKNOWN };
		std::size_t bestRank{ NoIndex };
		bool foundOversizedDecodedImage{};
		std::vector<wchar_t> longFileName(LongArchiveNameCapacity);
		auto header = std::make_unique<RARHeaderDataEx>();

		for (std::size_t archiveIndex{}; archiveIndex < catalog.Entries.size(); ++archiveIndex)
		{
			*header = {};
			longFileName.front() = L'\0';
			header->FileNameEx = longFileName.data();
			header->FileNameExSize = static_cast<unsigned int>(longFileName.size());
			const int headerResult = RARReadHeaderEx(archive.Get(), header.get());
			if (headerResult != ERAR_SUCCESS)
				return MakeRarError(L"Unable to reread the CBR archive", headerResult);
			if (!HeaderMatchesRecord(*header, catalog.Entries[archiveIndex]))
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
					L"The CBR changed while LiveIcons was selecting its cover." };

			const std::size_t candidateIndex = candidateByArchiveIndex[archiveIndex];
			const ImageCandidate* candidate = candidateIndex == NoIndex
				? nullptr
				: &catalog.Images[candidateIndex];
			if (candidate != nullptr &&
				GetHeaderFileName(*header, longFileName) != candidate->Name)
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
					L"A CBR image name changed while LiveIcons was selecting its cover." };

			const std::uint64_t uncompressedSize = GetUncompressedSize(*header);
			const bool captureImage = candidate != nullptr && candidate->Rank < bestRank &&
				attemptCandidate[candidateIndex] != 0;
			std::vector<char> imageData;
			if (captureImage)
			{
				try
				{
					imageData.reserve(static_cast<std::size_t>(uncompressedSize));
				}
				catch (const std::bad_alloc&)
				{
					return Parser::Result{ E_OUTOFMEMORY,
						L"Not enough memory was available to reserve a CBR image buffer." };
				}
				callback.Reset(&imageData, static_cast<std::size_t>(uncompressedSize));
			}
			else
			{
				callback.Reset();
			}

			const bool testEntry = captureImage || catalog.Solid ||
				(header->Flags & RHDF_SOLID) != 0;
			if (testEntry && ((header->Flags & RHDF_ENCRYPTED) != 0 ||
				header->DictSize > MaximumDictionaryKilobytes))
			{
				if (bestBitmap.Get() != nullptr)
					return TakeCoverResult(bestBitmap, bestAlpha);
				if ((header->Flags & RHDF_ENCRYPTED) != 0)
					return MakeEncryptedError();
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),
					L"The CBR requires a RAR dictionary larger than LiveIcons' 128 MiB safety limit." };
			}
			if (testEntry && !budget.ReserveDeclared(uncompressedSize))
			{
				if (bestBitmap.Get() != nullptr)
					return TakeCoverResult(bestBitmap, bestAlpha);
				return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
					L"CBR processing would exceed LiveIcons' 512 MiB decompression-work safety limit." };
			}

			const int processResult = RARProcessFileW(
				archive.Get(), testEntry ? RAR_TEST : RAR_SKIP, nullptr, nullptr);
			if (HasCallbackError(callback))
			{
				if (bestBitmap.Get() != nullptr &&
					(callback.PasswordRequested || callback.LargeDictionary ||
						callback.ProcessingLimitExceeded))
					return TakeCoverResult(bestBitmap, bestAlpha);
				return MakeCallbackError(callback);
			}
			if (processResult == ERAR_MISSING_PASSWORD || processResult == ERAR_BAD_PASSWORD ||
				((header->Flags & RHDF_ENCRYPTED) != 0 && processResult != ERAR_SUCCESS))
				return MakeEncryptedError();
			if (processResult != ERAR_SUCCESS)
				return MakeRarError(L"Unable to process a ranked CBR entry", processResult);

			if (captureImage)
			{
				if (imageData.size() != static_cast<std::size_t>(uncompressedSize))
					return Parser::Result{ HRESULT_FROM_WIN32(ERROR_READ_FAULT),
						L"UnRAR returned an incomplete CBR image entry." };

				HBITMAP rawBitmap{};
				WTS_ALPHATYPE alphaType{ WTSAT_UNKNOWN };
				SIZE imageSize{};
				const HRESULT imageResult = Gfx::LoadImageToHBitmap(
					imageData.data(), imageData.size(), rawBitmap, alphaType, imageSize);
				UniqueBitmap bitmap{ rawBitmap };
				if (imageResult == E_OUTOFMEMORY)
					return Parser::Result{ E_OUTOFMEMORY,
						L"Not enough memory was available to decode a CBR cover candidate." };
				if (FAILED(imageResult) || bitmap.Get() == nullptr)
				{
					foundOversizedDecodedImage = foundOversizedDecodedImage ||
						imageResult == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
				}
				else if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
				{
					bestBitmap.Reset(bitmap.Release());
					bestAlpha = alphaType;
					bestRank = candidate->Rank;
				}
			}

			if (bestBitmap.Get() != nullptr &&
				bestRemainingRank[archiveIndex + 1] >= bestRank)
				return TakeCoverResult(bestBitmap, bestAlpha);
		}

		*header = {};
		const int finalHeaderResult = RARReadHeaderEx(archive.Get(), header.get());
		if (finalHeaderResult != ERAR_END_ARCHIVE)
			return MakeRarError(L"The CBR catalog no longer matches the archive", finalHeaderResult);
		if (bestBitmap.Get() != nullptr)
			return TakeCoverResult(bestBitmap, bestAlpha);
		if (catalog.FoundEncryptedImage)
			return MakeEncryptedError();
		if (catalog.FoundExcessiveDictionaryImage)
			return Parser::Result{ HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),
				L"The CBR contains image entries requiring a RAR dictionary larger than LiveIcons' 128 MiB safety limit, and no usable cover was found." };
		if (catalog.FoundOversizedImage || foundOversizedDecodedImage)
			return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
				L"The CBR contains image entries, but at least one encoded or decoded image exceeds LiveIcons' 64 MiB in-memory safety limit and no valid cover was found." };
		if (excludedCandidateByWorkBudget)
			return Parser::Result{ HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
				L"No valid CBR cover was found within LiveIcons' 512 MiB decompression-work safety limit." };
		return Parser::Result{ HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
			L"The CBR archive contains images, but none could be decoded as a valid cover." };
	}

	template<typename Operation>
	[[nodiscard]] Parser::Result ExecuteParserBoundary(Operation&& operation) noexcept
	{
		try
		{
			return std::forward<Operation>(operation)();
		}
		catch (const std::bad_alloc&)
		{
			return Parser::Result{ E_OUTOFMEMORY };
		}
		catch (...)
		{
			return Parser::Result{ E_FAIL };
		}
	}

	HRESULT WriteAll(const HANDLE file, const char* bytes, const ULONG byteCount) noexcept
	{
		if (file == nullptr || file == INVALID_HANDLE_VALUE)
			return E_HANDLE;
		if (bytes == nullptr && byteCount != 0)
			return E_POINTER;

		ULONG writtenTotal{};
		while (writtenTotal < byteCount)
		{
			DWORD written{};
			if (!WriteFile(file, bytes + writtenTotal, byteCount - writtenTotal, &written, nullptr))
				return HRESULT_FROM_WIN32(GetLastError());
			if (written == 0)
				return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
			writtenTotal += written;
		}
		return S_OK;
	}

	HRESULT SnapshotStream(
		IStream* const stream, TemporaryFile& temporaryFile, std::wstring& outError)
	{
		StreamPositionRestorer position{ stream };
		HRESULT result = position.GetCaptureResult();
		if (FAILED(result))
		{
			outError = L"The CBR stream does not expose a restorable seek position.";
			return result;
		}

		if (result = position.Rewind(); FAILED(result))
			outError = L"The CBR stream could not be rewound before creating its temporary snapshot.";

		if (SUCCEEDED(result))
		{
			ULONGLONG streamSize{};
			if (SUCCEEDED(Utility::GetIStreamFileSize(stream, streamSize)) &&
				streamSize > MaximumArchiveSnapshotSize)
			{
				result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
				outError = L"The CBR stream exceeds LiveIcons' 1 GiB temporary-snapshot safety limit.";
			}
		}

		UniqueHandle output;
		if (SUCCEEDED(result) && FAILED(result = temporaryFile.Create(output)))
			outError = L"A uniquely named temporary file for the CBR stream could not be created.";

		std::vector<char> buffer;
		if (SUCCEEDED(result))
		{
			try
			{
				buffer.resize(StreamCopyBufferSize);
			}
			catch (const std::bad_alloc&)
			{
				result = E_OUTOFMEMORY;
				outError = L"Not enough memory was available to copy the CBR stream.";
			}
		}

		std::uint64_t snapshotSize{};
		while (SUCCEEDED(result))
		{
			ULONG bytesRead{};
			const HRESULT readResult = stream->Read(
				buffer.data(), static_cast<ULONG>(buffer.size()), &bytesRead);
			if (FAILED(readResult))
			{
				result = readResult;
				outError = L"The CBR stream could not be read while creating its temporary snapshot.";
				break;
			}
			if (bytesRead > buffer.size())
			{
				result = E_UNEXPECTED;
				outError = L"The CBR stream reported reading more data than its supplied buffer can hold.";
				break;
			}

			if (bytesRead > MaximumArchiveSnapshotSize - snapshotSize)
			{
				result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
				outError = L"The CBR stream exceeds LiveIcons' 1 GiB temporary-snapshot safety limit.";
				break;
			}
			snapshotSize += bytesRead;

			if (bytesRead != 0 && FAILED(result = WriteAll(output.Get(), buffer.data(), bytesRead)))
			{
				outError = L"The temporary CBR snapshot could not be written completely.";
				break;
			}

			if (readResult == S_FALSE || bytesRead == 0)
				break;
		}

		if (SUCCEEDED(result) && FAILED(result = output.Close()))
			outError = L"The temporary CBR snapshot could not be closed after writing.";

		const HRESULT restoreResult = position.Restore();
		if (FAILED(restoreResult))
		{
			if (SUCCEEDED(result))
			{
				result = restoreResult;
				outError = L"The original CBR stream position could not be restored after snapshotting.";
			}
			else
			{
				outError.append(L" Its original stream position also could not be restored.");
			}
		}

		return result;
	}
}

namespace Parser
{
	bool Cbr::CanParse(const std::wstring& fileExtension)
	{
		return EqualsAsciiCaseInsensitive(fileExtension, L".cbr");
	}

	Result Cbr::Parse(const std::wstring& fileName)
	{
		return ExecuteParserBoundary([&]() -> Result
		{
			if (fileName.empty())
				return Result{ E_INVALIDARG, L"The CBR path is empty." };

			const std::scoped_lock unrarLock{ GetUnrarMutex() };
			WorkBudget budget;
			CallbackContext callback{ budget };
			ArchiveCatalog catalog;
			Result catalogResult = CatalogArchive(fileName, callback, catalog);
			if (FAILED(catalogResult.HResult))
				return catalogResult;

			if (ShouldReadComicInfo(catalog))
			{
				std::vector<char> comicInfo;
				Result metadataResult = ReadCatalogEntry(
					fileName,
					catalog,
					*catalog.ComicInfoArchiveIndex,
					static_cast<std::size_t>(MaximumComicInfoSize),
					budget,
					comicInfo);
				if (SUCCEEDED(metadataResult.HResult))
				{
					AssignCandidateRanks(
						catalog,
						CbrCoverSelection::FindFrontCoverImageIndices(
							std::string_view{ comicInfo.data(), comicInfo.size() },
							catalog.Images.size()));
				}
				else if (metadataResult.HResult == E_OUTOFMEMORY ||
					metadataResult.HResult == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))
				{
					return metadataResult;
				}
			}

			return ExtractRankedCover(fileName, catalog, budget);
		});
	}

	Result Cbr::Parse(IStream* const stream)
	{
		return ExecuteParserBoundary([&]() -> Result
		{
			if (stream == nullptr)
				return Result{ E_POINTER, L"The CBR stream is null." };

			TemporaryFile temporaryFile;
			std::wstring snapshotError;
			if (const HRESULT result = SnapshotStream(stream, temporaryFile, snapshotError); FAILED(result))
				return Result{ result, std::move(snapshotError) };

			return Parse(temporaryFile.GetPath());
		});
	}
}
