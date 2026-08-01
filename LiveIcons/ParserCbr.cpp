#include "pch.h"
#include "ParserCbr.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

#include "Gfx.h"

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
	constexpr std::uint64_t MaximumArchiveEntries{ 10000ULL };
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

		~UniqueHandle()
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

	class TemporaryFile final
	{
		std::wstring Path;

	public:
		TemporaryFile() = default;
		TemporaryFile(const TemporaryFile&) = delete;
		TemporaryFile(TemporaryFile&&) = delete;
		TemporaryFile& operator=(const TemporaryFile&) = delete;
		TemporaryFile& operator=(TemporaryFile&&) = delete;

		~TemporaryFile()
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

			Path.assign(temporaryName.data());
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
		explicit StreamPositionRestorer(IStream* const stream) :
			Stream{ stream }, CaptureResult{ E_POINTER }
		{
			if (Stream == nullptr)
				return;

			constexpr LARGE_INTEGER zero{};
			CaptureResult = Stream->Seek(zero, STREAM_SEEK_CUR, &OriginalPosition);
		}

		StreamPositionRestorer(const StreamPositionRestorer&) = delete;
		StreamPositionRestorer(StreamPositionRestorer&&) = delete;
		StreamPositionRestorer& operator=(const StreamPositionRestorer&) = delete;
		StreamPositionRestorer& operator=(StreamPositionRestorer&&) = delete;

		~StreamPositionRestorer()
		{
			static_cast<void>(Restore());
		}

		[[nodiscard]] HRESULT GetCaptureResult() const noexcept { return CaptureResult; }

		HRESULT Rewind() const
		{
			if (FAILED(CaptureResult))
				return CaptureResult;
			constexpr LARGE_INTEGER zero{};
			return Stream->Seek(zero, STREAM_SEEK_SET, nullptr);
		}

		HRESULT Restore()
		{
			if (Restored || FAILED(CaptureResult))
				return CaptureResult;

			LARGE_INTEGER position{};
			position.QuadPart = static_cast<LONGLONG>(OriginalPosition.QuadPart);
			const HRESULT result = Stream->Seek(position, STREAM_SEEK_SET, nullptr);
			if (SUCCEEDED(result))
				Restored = true;
			return result;
		}
	};

	class RarArchive final
	{
		HANDLE Handle{ nullptr };

	public:
		explicit RarArchive(const HANDLE handle) noexcept : Handle{ handle } { }
		RarArchive(const RarArchive&) = delete;
		RarArchive(RarArchive&&) = delete;
		RarArchive& operator=(const RarArchive&) = delete;
		RarArchive& operator=(RarArchive&&) = delete;

		~RarArchive()
		{
			if (Handle != nullptr)
				RARCloseArchive(Handle);
		}

		[[nodiscard]] HANDLE Get() const noexcept { return Handle; }
	};

	struct CallbackContext final
	{
		std::vector<char>* Output{};
		bool CaptureData{};
		bool DataTooLarge{};
		bool AllocationFailed{};
		bool PasswordRequested{};
		bool MissingVolume{};
		bool LargeDictionary{};
		bool ProcessingLimitExceeded{};
		std::uint64_t ProcessedBytes{};

		void Reset(std::vector<char>* const output = nullptr) noexcept
		{
			Output = output;
			CaptureData = output != nullptr;
			DataTooLarge = false;
			AllocationFailed = false;
			PasswordRequested = false;
			MissingVolume = false;
			LargeDictionary = false;
			ProcessingLimitExceeded = false;
		}
	};

	int CALLBACK UnrarCallback(
		const UINT message, const LPARAM userData, const LPARAM parameter1,
		const LPARAM parameter2) noexcept
	{
		auto* const context = reinterpret_cast<CallbackContext*>(userData);
		if (context == nullptr)
			return -1;

		switch (message)
		{
		case UCM_PROCESSDATA:
		{
			if (parameter2 < 0 || parameter1 == 0 && parameter2 != 0)
				return -1;

			const auto byteCount = static_cast<std::size_t>(parameter2);
			if (byteCount > MaximumProcessedBytes ||
				context->ProcessedBytes > MaximumProcessedBytes - byteCount)
			{
				context->ProcessingLimitExceeded = true;
				return -1;
			}
			context->ProcessedBytes += byteCount;

			if (!context->CaptureData || context->Output == nullptr || byteCount == 0)
				return 0;

			if (byteCount > MaximumImageSize ||
				context->Output->size() > MaximumImageSize - byteCount)
			{
				context->DataTooLarge = true;
				return -1;
			}

			const auto* const bytes = reinterpret_cast<const char*>(parameter1);
			try
			{
				context->Output->insert(context->Output->end(), bytes, bytes + byteCount);
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
			return 0;
		}

		case UCM_NEEDPASSWORD:
		case UCM_NEEDPASSWORDW:
			context->PasswordRequested = true;
			return -1;

		case UCM_CHANGEVOLUME:
		case UCM_CHANGEVOLUMEW:
			if (parameter2 == RAR_VOL_ASK)
			{
				context->MissingVolume = true;
				return -1;
			}
			return 0;

		case UCM_LARGEDICT:
			context->LargeDictionary = true;
			return 0; // UCM_LARGEDICT requires 1, rather than 0, to permit extraction.

		default:
			return 0;
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
			return std::wstring{ longName.data() };
		return std::wstring{ header.FileNameW };
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
		if (callback.DataTooLarge)
			return Parser::Result{
				HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
				L"An image entry expanded beyond LiveIcons' 64 MiB in-memory safety limit."
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
		return Parser::Result{ E_FAIL, L"UnRAR stopped while processing a CBR entry." };
	}

	HRESULT WriteAll(const HANDLE file, const char* bytes, const ULONG byteCount)
	{
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
			STATSTG statistics{};
			if (SUCCEEDED(stream->Stat(&statistics, STATFLAG_NONAME)) &&
				statistics.cbSize.QuadPart > MaximumArchiveSnapshotSize)
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
		return StrLib::EqualsCi(fileExtension, std::wstring{ L".cbr" });
	}

	Result Cbr::Parse(const std::wstring& fileName)
	{
		if (fileName.empty())
			return Result{ E_INVALIDARG, L"The CBR path is empty." };

		const std::scoped_lock unrarLock{ GetUnrarMutex() };
		CallbackContext callback{};
		std::wstring mutableFileName{ fileName };
		RAROpenArchiveDataEx openData{};
		openData.ArcNameW = mutableFileName.data();
		openData.OpenMode = RAR_OM_EXTRACT;
		openData.Callback = UnrarCallback;
		openData.UserData = reinterpret_cast<LPARAM>(&callback);

		const HANDLE rawArchive = RAROpenArchiveEx(&openData);
		if (rawArchive == nullptr)
		{
			if (callback.PasswordRequested ||
				openData.OpenResult == ERAR_MISSING_PASSWORD || openData.OpenResult == ERAR_BAD_PASSWORD)
				return MakeEncryptedError();
			if (callback.MissingVolume || callback.LargeDictionary)
				return MakeCallbackError(callback);
			return MakeRarError(L"Unable to open the CBR archive", openData.OpenResult);
		}

		RarArchive archive{ rawArchive };
		if ((openData.Flags & ROADF_ENCHEADERS) != 0)
			return MakeEncryptedError();

		const bool solidArchive = (openData.Flags & ROADF_SOLID) != 0;
		bool foundImageEntry{};
		bool foundOversizedImage{};
		bool foundOversizedDecodedImage{};
		bool foundEncryptedImage{};
		std::uint64_t archiveEntryCount{};
		std::uint64_t declaredProcessedBytes{};
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
			if (++archiveEntryCount > MaximumArchiveEntries)
				return Result{
					HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
					L"The CBR contains more than LiveIcons' 10,000-entry safety limit."
				};

			const std::wstring entryName = GetHeaderFileName(header, longFileName);
			const bool isDirectory = (header.Flags & RHDF_DIRECTORY) != 0;
			const bool isImage = !isDirectory && IsImageFileName(entryName);
			const bool isEncrypted = (header.Flags & RHDF_ENCRYPTED) != 0;
			const std::uint64_t uncompressedSize = GetUncompressedSize(header);

			foundImageEntry = foundImageEntry || isImage;
			foundEncryptedImage = foundEncryptedImage || isImage && isEncrypted;
			foundOversizedImage = foundOversizedImage || isImage && uncompressedSize > MaximumImageSize;

			std::vector<char> imageData;
			const bool collectImage = isImage && !isEncrypted && uncompressedSize <= MaximumImageSize;
			if (collectImage)
			{
				try
				{
					imageData.reserve(static_cast<std::size_t>(uncompressedSize));
				}
				catch (const std::bad_alloc&)
				{
					return Result{ E_OUTOFMEMORY, L"Not enough memory was available to reserve a CBR image buffer." };
				}
				callback.Reset(&imageData);
			}
			else
			{
				callback.Reset();
			}

			// A solid archive must actually decode preceding entries. Use RAR_TEST so
			// UCM_PROCESSDATA can enforce a cumulative work budget while discarding
			// non-image bytes. Non-solid entries can be skipped without decompression.
			const bool testEntry = collectImage || solidArchive || (header.Flags & RHDF_SOLID) != 0;
			if (testEntry &&
				(uncompressedSize > MaximumProcessedBytes ||
					declaredProcessedBytes > MaximumProcessedBytes - uncompressedSize))
			{
				return Result{
					HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
					L"CBR processing would exceed LiveIcons' 512 MiB decompression-work safety limit."
				};
			}
			if (testEntry)
				declaredProcessedBytes += uncompressedSize;

			const int processResult = RARProcessFileW(
				archive.Get(), testEntry ? RAR_TEST : RAR_SKIP, nullptr, nullptr);
			if (callback.PasswordRequested || callback.DataTooLarge ||
				callback.AllocationFailed || callback.MissingVolume || callback.LargeDictionary ||
				callback.ProcessingLimitExceeded)
				return MakeCallbackError(callback);
			if (processResult == ERAR_MISSING_PASSWORD || processResult == ERAR_BAD_PASSWORD ||
				isEncrypted && processResult != ERAR_SUCCESS)
				return MakeEncryptedError();
			if (processResult != ERAR_SUCCESS)
				return MakeRarError(
					std::format(L"Unable to process CBR entry '{}'", entryName), processResult);

			if (!collectImage || imageData.empty())
				continue;

			HBITMAP bitmap{};
			WTS_ALPHATYPE alphaType{ WTSAT_UNKNOWN };
			SIZE imageSize{};
			const HRESULT imageResult = Gfx::LoadImageToHBitmap(
				imageData.data(), imageData.size(), bitmap, alphaType, imageSize);
			if (FAILED(imageResult) || bitmap == nullptr)
			{
				foundOversizedDecodedImage = foundOversizedDecodedImage ||
					imageResult == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
				if (bitmap != nullptr)
					DeleteObject(bitmap);
				continue;
			}

			if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
				return Result{ std::wstring{}, bitmap, alphaType };

			DeleteObject(bitmap);
		}

		if (foundEncryptedImage)
			return MakeEncryptedError();
		if (foundOversizedImage || foundOversizedDecodedImage)
			return Result{
				HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
				L"The CBR contains image entries, but at least one encoded or decoded image exceeds LiveIcons' 64 MiB in-memory safety limit and no valid cover was found."
			};
		if (!foundImageEntry)
			return Result{
				HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
				L"The CBR archive does not contain a supported image entry."
			};
		return Result{
			HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
			L"The CBR archive contains images, but none could be decoded as a valid cover."
		};
	}

	Result Cbr::Parse(IStream* const stream)
	{
		if (stream == nullptr)
			return Result{ E_POINTER, L"The CBR stream is null." };

		TemporaryFile temporaryFile;
		std::wstring snapshotError;
		if (const HRESULT result = SnapshotStream(stream, temporaryFile, snapshotError); FAILED(result))
			return Result{ result, std::move(snapshotError) };

		return Parse(temporaryFile.GetPath());
	}
}
