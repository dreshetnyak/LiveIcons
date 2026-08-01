#include "pch.h"
#include "Utility.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace
{
	constexpr std::uint64_t MaximumInputBytes{ 512ULL * 1024ULL * 1024ULL };
	constexpr ULONG StreamChunkSize{ 64UL * 1024UL };

	[[nodiscard]] HRESULT HResultFromLastError(
		const DWORD fallback = ERROR_GEN_FAILURE) noexcept
	{
		const DWORD error = GetLastError();
		return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : fallback);
	}

	class StatNameOwner final
	{
		STATSTG& Stat;

	public:
		explicit StatNameOwner(STATSTG& stat) noexcept : Stat{ stat } { }
		StatNameOwner(const StatNameOwner&) = delete;
		StatNameOwner& operator=(const StatNameOwner&) = delete;
		~StatNameOwner() noexcept { CoTaskMemFree(Stat.pwcsName); }
	};

	class StreamPositionRestorer final
	{
		IStream* Stream;
		ULARGE_INTEGER OriginalPosition{};
		bool Captured{};

	public:
		explicit StreamPositionRestorer(IStream* stream) noexcept : Stream{ stream } { }
		StreamPositionRestorer(const StreamPositionRestorer&) = delete;
		StreamPositionRestorer& operator=(const StreamPositionRestorer&) = delete;

		~StreamPositionRestorer() noexcept
		{
			static_cast<void>(Restore());
		}

		[[nodiscard]] HRESULT Capture() noexcept
		{
			if (Stream == nullptr)
				return E_POINTER;
			try
			{
				constexpr LARGE_INTEGER current{};
				const HRESULT result = Stream->Seek(
					current, STREAM_SEEK_CUR, &OriginalPosition);
				Captured = SUCCEEDED(result);
				return result;
			}
			catch (const std::bad_alloc&)
			{
				return E_OUTOFMEMORY;
			}
			catch (...)
			{
				return E_UNEXPECTED;
			}
		}

		[[nodiscard]] HRESULT SeekToBeginning() noexcept
		{
			if (Stream == nullptr)
				return E_POINTER;
			try
			{
				constexpr LARGE_INTEGER beginning{};
				return Stream->Seek(beginning, STREAM_SEEK_SET, nullptr);
			}
			catch (const std::bad_alloc&)
			{
				return E_OUTOFMEMORY;
			}
			catch (...)
			{
				return E_UNEXPECTED;
			}
		}

		[[nodiscard]] HRESULT Restore() noexcept
		{
			if (!Captured)
				return S_OK;
			Captured = false;
			if (OriginalPosition.QuadPart >
				static_cast<ULONGLONG>((std::numeric_limits<LONGLONG>::max)()))
				return STG_E_INVALIDFUNCTION;
			try
			{
				LARGE_INTEGER original{};
				original.QuadPart = static_cast<LONGLONG>(OriginalPosition.QuadPart);
				return Stream->Seek(original, STREAM_SEEK_SET, nullptr);
			}
			catch (const std::bad_alloc&)
			{
				return E_OUTOFMEMORY;
			}
			catch (...)
			{
				return E_UNEXPECTED;
			}
		}
	};

	[[nodiscard]] HRESULT ReadStreamExactly(
		IStream* stream,
		char* destination,
		const std::size_t size) noexcept
	{
		if (stream == nullptr || (destination == nullptr && size != 0))
			return E_POINTER;

		std::size_t totalRead{};
		while (totalRead < size)
		{
			const ULONG requested = static_cast<ULONG>((std::min)(
				size - totalRead,
				static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())));
			ULONG bytesRead{};
			HRESULT result;
			try
			{
				result = stream->Read(destination + totalRead, requested, &bytesRead);
			}
			catch (const std::bad_alloc&)
			{
				return E_OUTOFMEMORY;
			}
			catch (...)
			{
				return E_UNEXPECTED;
			}

			if (FAILED(result))
				return result;
			if (bytesRead == 0 || bytesRead > requested)
				return STG_E_READFAULT;
			totalRead += bytesRead;
		}

		return S_OK;
	}

	[[nodiscard]] HRESULT WriteHandleExactly(
		const HANDLE file,
		const char* source,
		const DWORD size) noexcept
	{
		DWORD totalWritten{};
		while (totalWritten < size)
		{
			DWORD written{};
			if (!WriteFile(file, source + totalWritten, size - totalWritten, &written, nullptr))
				return HResultFromLastError();
			if (written == 0 || written > size - totalWritten)
				return STG_E_WRITEFAULT;
			totalWritten += written;
		}
		return S_OK;
	}

	[[nodiscard]] HRESULT FinishWithRestore(
		const HRESULT operationResult,
		StreamPositionRestorer& position) noexcept
	{
		const HRESULT restoreResult = position.Restore();
		return FAILED(operationResult) ? operationResult : restoreResult;
	}

	class PendingTempFile final
	{
		const wchar_t* Path;
		bool Active{ true };

	public:
		explicit PendingTempFile(const wchar_t* path) noexcept : Path{ path } { }
		PendingTempFile(const PendingTempFile&) = delete;
		PendingTempFile& operator=(const PendingTempFile&) = delete;
		~PendingTempFile() noexcept
		{
			if (Active && Path != nullptr)
				DeleteFileW(Path);
		}
		void Release() noexcept { Active = false; }
	};
}

namespace Utility
{
    string ToAbsolutePath(const string& currentFilePath, const string& relativeFilePath)
    {
		if (relativeFilePath.empty())
			return {};

		auto current = currentFilePath;
		auto relative = relativeFilePath;
		std::ranges::replace(current, '\\', '/');
		std::ranges::replace(relative, '\\', '/');

		vector<string> pathComponents;
		for (auto& component : StrLib::Split(current, '/'))
			if (!component.empty() && component != ".")
				pathComponents.push_back(std::move(component));
		if (!pathComponents.empty())
			pathComponents.pop_back();
		if (!relative.empty() && relative.front() == '/')
			pathComponents.clear();

		for (auto& component : StrLib::Split(relative, '/'))
		{
			if (component.empty() || component == ".")
				continue;
			if (component == "..")
			{
				if (pathComponents.empty())
					return {};
				pathComponents.pop_back();
				continue;
			}
			pathComponents.push_back(std::move(component));
		}

		return StrLib::Join<char>(pathComponents, "/");
    }

    string TrimPathExtension(const string& path)
    {
        const auto extensionOffset = path.find_last_of('.');
        return extensionOffset != string::npos ? path.substr(0, extensionOffset) : path;
    }

    string UrlDecode(const string& urlEncoded)
    {
	    string decoded{};
	    decoded.resize(urlEncoded.size());

        char chLeft, chRight;
        size_t writeOffset = 0;
	    const auto urlEncodedSize = urlEncoded.size();
	    for (size_t readOffset = 0; readOffset < urlEncodedSize; ++readOffset)        
	    {
            char ch = urlEncoded[readOffset];
		    if (ch == '+')
			    ch = ' ';
		    else if (ch == '%' && (readOffset + 2 < urlEncodedSize && IsHex(chLeft = urlEncoded[readOffset + 1]) && IsHex(chRight = urlEncoded[readOffset + 2])))
		    {
			    ch = static_cast<char>(FromHex(chLeft) << 4 | FromHex(chRight));
			    readOffset += 2;
		    }

	    	decoded[writeOffset++] = ch;
        }

        decoded.resize(writeOffset);
	    return decoded;
    }

    HRESULT ReadFile(const std::wstring& fileFullName, std::vector<char>& outFileContent) noexcept
    {
		outFileContent.clear();
        try
        {
            std::ifstream fileStream{ fileFullName, ios::in | ios::binary | ios::ate };
            if (!fileStream.is_open())
                return E_FAIL;

            const auto endPosition = fileStream.tellg();
            if (endPosition < 0)
                return E_FAIL;

            const auto fileSize = static_cast<std::uintmax_t>(static_cast<std::streamoff>(endPosition));
			if (fileSize > MaximumInputBytes ||
				fileSize > outFileContent.max_size() ||
                fileSize > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

			std::vector<char> content(static_cast<size_t>(fileSize));
            fileStream.seekg(0, ios::beg);
			if (!content.empty())
				fileStream.read(content.data(), static_cast<std::streamsize>(content.size()));
			if (!content.empty() &&
				fileStream.gcount() != static_cast<std::streamsize>(content.size()))
				return STG_E_READFAULT;

			outFileContent.swap(content);
			return S_OK;
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

    HRESULT GetIStreamFileName(IStream* stream, wstring& outFileName) noexcept
    {
		outFileName.clear();
        if (stream == nullptr)
			return E_POINTER;
        STATSTG streamStat{};
		const StatNameOwner nameOwner{ streamStat };
		try
		{
			if (const auto result = stream->Stat(&streamStat, STATFLAG_DEFAULT); FAILED(result))
				return result;
			if (streamStat.pwcsName == nullptr)
				return STG_E_INVALIDPOINTER;
			wstring name{ streamStat.pwcsName };
			outFileName.swap(name);
			return S_OK;
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
    }

    HRESULT GetFileExtension(const wstring& fileName, wstring& outFileExtension) noexcept
    {
		outFileExtension.clear();
		try
		{
			const auto extensionOffset = fileName.find_last_of('.');
			if (extensionOffset == wstring::npos)
				return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
			wstring extension = fileName.substr(extensionOffset);
			outFileExtension.swap(extension);
			return S_OK;
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
    }

    HRESULT GetIStreamFileSize(IStream* stream, ULONGLONG& outSize) noexcept
    {
		outSize = 0;
        if (stream == nullptr)
			return E_POINTER;
		try
		{
			STATSTG streamStat{};
			const StatNameOwner nameOwner{ streamStat };
			if (const auto result = stream->Stat(&streamStat, STATFLAG_NONAME); FAILED(result))
				return result;
			outSize = streamStat.cbSize.QuadPart;
			return S_OK;
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
    }

    HRESULT ReadIStream(IStream* stream, std::vector<char>& outFileContent) noexcept
    {
		outFileContent.clear();
        ULONGLONG size{};
        if (const auto result = GetIStreamFileSize(stream, size); FAILED(result))
            return result;
		if (size > MaximumInputBytes || size > outFileContent.max_size() ||
			size > (std::numeric_limits<size_t>::max)())
			return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

		try
		{
			std::vector<char> content(static_cast<size_t>(size));
			StreamPositionRestorer position{ stream };
			if (const HRESULT result = position.Capture(); FAILED(result))
				return result;
			if (const HRESULT result = position.SeekToBeginning(); FAILED(result))
				return FinishWithRestore(result, position);
			const HRESULT result = ReadStreamExactly(stream, content.data(), content.size());
			const HRESULT finalResult = FinishWithRestore(result, position);
			if (FAILED(finalResult))
				return finalResult;
			outFileContent.swap(content);
			return S_OK;
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
    }

    HRESULT ReadIStream(IStream* stream, const HANDLE outFileHandle) noexcept
    {
		if (stream == nullptr || outFileHandle == nullptr || outFileHandle == INVALID_HANDLE_VALUE)
			return E_POINTER;
        ULONGLONG size{};
        if (const auto result = GetIStreamFileSize(stream, size); FAILED(result))
            return result;
		if (size > MaximumInputBytes)
			return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

		StreamPositionRestorer position{ stream };
		if (const HRESULT result = position.Capture(); FAILED(result))
			return result;
		if (const HRESULT result = position.SeekToBeginning(); FAILED(result))
			return FinishWithRestore(result, position);

		constexpr LARGE_INTEGER beginning{};
		if (!SetFilePointerEx(outFileHandle, beginning, nullptr, FILE_BEGIN) ||
			!SetEndOfFile(outFileHandle))
			return FinishWithRestore(HResultFromLastError(), position);

		const std::unique_ptr<char[]> buffer{
			new (std::nothrow) char[StreamChunkSize] };
		if (!buffer)
			return FinishWithRestore(E_OUTOFMEMORY, position);
		std::uint64_t totalRead{};
		while (totalRead < size)
		{
			const ULONG requested = static_cast<ULONG>((std::min)(
				size - totalRead, static_cast<ULONGLONG>(StreamChunkSize)));
			ULONG bytesRead{};
			HRESULT readResult;
			try
			{
				readResult = stream->Read(buffer.get(), requested, &bytesRead);
			}
			catch (const std::bad_alloc&)
			{
				return FinishWithRestore(E_OUTOFMEMORY, position);
			}
			catch (...)
			{
				return FinishWithRestore(E_UNEXPECTED, position);
			}
			if (FAILED(readResult))
				return FinishWithRestore(readResult, position);
			if (bytesRead == 0 || bytesRead > requested)
				return FinishWithRestore(STG_E_READFAULT, position);
			if (const HRESULT writeResult = WriteHandleExactly(
				outFileHandle, buffer.get(), bytesRead); FAILED(writeResult))
				return FinishWithRestore(writeResult, position);
			totalRead += bytesRead;
		}

		if (!SetEndOfFile(outFileHandle) ||
			!SetFilePointerEx(outFileHandle, beginning, nullptr, FILE_BEGIN))
			return FinishWithRestore(HResultFromLastError(), position);
		return FinishWithRestore(S_OK, position);
    }

    HRESULT DecodeBase64(const string& base64Encoded, vector<char>& outDecoded) noexcept
    {
		outDecoded.clear();
		if (base64Encoded.empty())
			return E_INVALIDARG;
		if (base64Encoded.size() > (std::numeric_limits<DWORD>::max)())
			return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

		try
		{
			const DWORD encodedSize = static_cast<DWORD>(base64Encoded.size());
			DWORD decodedSize{};
			if (!CryptStringToBinaryA(
				base64Encoded.data(), encodedSize, CRYPT_STRING_BASE64,
				nullptr, &decodedSize, nullptr, nullptr))
				return HResultFromLastError(ERROR_INVALID_DATA);
			if (decodedSize > MaximumInputBytes)
				return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

			vector<char> decoded(decodedSize);
			if (!CryptStringToBinaryA(
				base64Encoded.data(), encodedSize, CRYPT_STRING_BASE64,
				reinterpret_cast<BYTE*>(decoded.data()), &decodedSize, nullptr, nullptr))
				return HResultFromLastError(ERROR_INVALID_DATA);
			decoded.resize(decodedSize);
			outDecoded.swap(decoded);
			return S_OK;
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
    }

    HRESULT GetTempFileFullName(wstring& outTempFileName) noexcept
    {
		outTempFileName.clear();
		try
		{
			const DWORD required = GetTempPathW(0, nullptr);
			if (required == 0)
				return HResultFromLastError();
			if (required > 32768)
				return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);

			wstring tempDirectory(required, L'\0');
			const DWORD length = GetTempPathW(required, tempDirectory.data());
			if (length == 0)
				return HResultFromLastError();
			if (length >= required)
				return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
			tempDirectory.resize(length);

			std::array<wchar_t, MAX_PATH + 1> tempPath{};
			if (GetTempFileNameW(tempDirectory.c_str(), L"LIC", 0, tempPath.data()) == 0)
				return HResultFromLastError();
			PendingTempFile pendingFile{ tempPath.data() };
			outTempFileName.assign(tempPath.data());
			pendingFile.Release();
			return S_OK;
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
    }

    bool TryParseNumber(const string& numberStr, size_t& number) noexcept
    {
		if (numberStr.empty())
			return false;
		unsigned long long parsed{};
		const auto [end, error] = std::from_chars(
			numberStr.data(), numberStr.data() + numberStr.size(), parsed);
		if (error != std::errc{} || end != numberStr.data() + numberStr.size() ||
			parsed > (std::numeric_limits<size_t>::max)())
			return false;
		number = static_cast<size_t>(parsed);
		return true;
    }
}
