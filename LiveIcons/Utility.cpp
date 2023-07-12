#include "pch.h"
#include "Utility.h"
#include "GlobalMem.h"

namespace Utility
{
    wstring GetLastErrorMessage()
    {
        return GetLastErrorMessage(GetLastError());
    }

    wstring GetLastErrorMessage(const DWORD lastErrorCode)
    {
        LPWSTR errorMessageBuffer = nullptr;
        if (!FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, lastErrorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&errorMessageBuffer), 0, nullptr) ||
            errorMessageBuffer == nullptr)
            return wstring{};

        auto errorMessage = wstring(errorMessageBuffer);
        LocalFree(errorMessageBuffer);
        return errorMessage;
    }

    string ToAbsolutePath(const string& currentFilePath, const string& relativeFilePath)
    {
        const auto relativePathSplit = StrLib::Split(relativeFilePath, '/');
        auto absolutePathSplit = StrLib::Split(currentFilePath, '/');
        absolutePathSplit.pop_back(); // Remove the file name from the path

        for (const auto& relativePathItem : relativePathSplit)
        {
            if (relativePathItem != "..")
                absolutePathSplit.push_back(relativePathItem);
            else
                absolutePathSplit.pop_back();
        }

        return StrLib::Join<char>(absolutePathSplit, "/");
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

    HRESULT ReadFile(const std::wstring& fileFullName, std::vector<char>& outFileContent)
    {
        std::ifstream fileStream{ fileFullName, ios::in | ios::binary | ios::ate };
        if (!fileStream.is_open())
            return E_FAIL;
        outFileContent.reserve(fileStream.tellg());
        fileStream.seekg(0);
        std::copy(std::istream_iterator<char>(fileStream), std::istream_iterator<char>(), std::back_inserter(outFileContent));
        fileStream.close();
        return S_OK;
    }

    HRESULT GetIStreamFileName(IStream* stream, wstring& outFileName)
    {
        if (stream == nullptr)
            return E_FAIL;
        STATSTG streamStat{};
        if (const auto result = stream->Stat(&streamStat, STATFLAG_DEFAULT); FAILED(result))
            return result;
        outFileName = std::wstring{ streamStat.pwcsName };
        return S_OK;
    }

    HRESULT GetFileExtension(const wstring& fileName, wstring& outFileExtension)
    {
        const auto extensionOffset = fileName.find_last_of('.');
        if (extensionOffset == std::string::npos)
            return E_FAIL;
        outFileExtension = fileName.substr(extensionOffset);
        return S_OK;
    }

    HRESULT GetIStreamFileSize(IStream* stream, ULONGLONG& outSize)
    {
        if (stream == nullptr)
            return E_FAIL;
        STATSTG streamStat{};
        if (const auto result = stream->Stat(&streamStat, STATFLAG_DEFAULT); FAILED(result))
            return result;
        outSize = streamStat.cbSize.QuadPart;
        return S_OK;
    }

    HRESULT GetIStreamFileNameAndSize(IStream* stream, ULONGLONG& outSize, wstring& outFileName)
    {
        if (stream == nullptr)
            return E_FAIL;
        STATSTG streamStat{};
        if (const auto result = stream->Stat(&streamStat, STATFLAG_DEFAULT); FAILED(result))
            return result;
        outSize = streamStat.cbSize.QuadPart;
        outFileName = std::wstring{ streamStat.pwcsName };
        return S_OK;
    }

    HRESULT SeekToBeginning(IStream* stream)
    {
        constexpr LARGE_INTEGER position{ { 0, 0 } };
        ULARGE_INTEGER currentPosition{};
        return stream->Seek(position, STREAM_SEEK_SET, &currentPosition);
    }

    HRESULT ReadIStream(IStream* stream, std::vector<char>& outFileContent)
    {
        ULONGLONG size{};
        if (const auto result = GetIStreamFileSize(stream, size); FAILED(result))
            return result;
        outFileContent.resize(size);
        return ReadIStream(stream, outFileContent.data(), outFileContent.size());
    }

    HRESULT ReadIStream(IStream* stream, char* outFileContent, const size_t fileSize)
    {
        if (const auto result = SeekToBeginning(stream); FAILED(result))
            return result;

        ULONG bytesRead{};
        if (const auto result = stream->Read(outFileContent, static_cast<ULONG>(fileSize), &bytesRead); FAILED(result))
            return result;

        static_cast<void>(SeekToBeginning(stream));
        return S_OK;
    }

    HRESULT ReadIStream(IStream* stream, const HANDLE outFileHandle)
    {
        ULONGLONG size{};
        if (const auto result = GetIStreamFileSize(stream, size); FAILED(result))
            return result;

        std::vector<char> content{};
        content.resize(size);
        ULONG bytesRead{};
        if (const auto result = stream->Read(content.data(), static_cast<ULONG>(size), &bytesRead); FAILED(result))
            return result;

        DWORD bytesWritten;
        if (!WriteFile(outFileHandle, content.data(), bytesRead, &bytesWritten, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());

        return SetFilePointer(outFileHandle, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER
            ? S_OK
            : HRESULT_FROM_WIN32(GetLastError());
    }

    HRESULT DecodeBase64(const string& base64Encoded, vector<char>& outDecoded)
    {
        Logger::Write("Utility::DecodeBase64: Starting.");

    	const auto encodedDataPtr = base64Encoded.data();
        const auto encodedSize = static_cast<DWORD>(base64Encoded.size());

        Logger::Write(format("Utility::DecodeBase64: Encoded size: {}", encodedSize));

    	DWORD dwDecodedSize = 0, dwSkipChars = 0, dwActualFormat = 0;        
        if (!CryptStringToBinaryA(
            encodedDataPtr,
            encodedSize,
            CRYPT_STRING_BASE64, 
            nullptr, 
            &dwDecodedSize, 
            &dwSkipChars, 
            &dwActualFormat))
            return E_FAIL;

        Logger::Write(format("Utility::DecodeBase64: Decoded size: {}", dwDecodedSize));
        outDecoded.resize(dwDecodedSize);

        return CryptStringToBinaryA(
            encodedDataPtr,
            encodedSize,
            CRYPT_STRING_BASE64,
            reinterpret_cast<BYTE*>(outDecoded.data()),
            &dwDecodedSize,
            &dwSkipChars,
            &dwActualFormat)
            ? S_OK
            : E_FAIL;
    }

    HRESULT GetTempFileFullName(wstring& outTempFileName)
    {
        const auto pathSize = GetTempPath(0, nullptr);
        const unique_ptr<TCHAR[]> path{ new TCHAR[pathSize] };
        if (const auto getPathResult = GetTempPath(pathSize, path.get()); getPathResult == 0 || getPathResult > pathSize)
            return HRESULT_FROM_WIN32(GetLastError());

        const unique_ptr<TCHAR[]> fullFileName{ new TCHAR[MAX_PATH + 1] };
        if (GetTempFileName(path.get(), L"ram", 1, fullFileName.get()) == 0)
            return HRESULT_FROM_WIN32(GetLastError());

        outTempFileName.assign(fullFileName.get());
        return S_OK;
    }

    bool TryParseNumber(const string& numberStr, size_t& number)
    {
        try
        {
            number = stoull(numberStr);
            return true;
        }
        catch (...)
        {
	        return false;
        }
    }
}

namespace Logger
{
    void Write(const string& message)
    {
        //OutputDebugString();
#ifdef _DEBUG
        WriteToFile("C:\\Projects\\LiveIcons\\x64\\Debug\\log.txt", format("{:%Y-%m-%d %T} {}", static_cast<chrono::sys_time<chrono::nanoseconds>>(chrono::system_clock::now()), message).c_str());
#endif
        //C:\Projects\LiveIcons\x64\Debug
    }

    std::mutex LogFileLock{};
    void WriteToFile(const string& filePath, const string& message)
    {
        LogFileLock.lock();
        ofstream file{ filePath, ios::out | ios::app };        
        if (file.fail())
        {
            LogFileLock.unlock();
	        return;
        }
        file << message << endl;
        file.close();
        LogFileLock.unlock();
    }
    
    void WriteFile(const string& filePath, const vector<char>& content)
    {
        WriteFile(filePath, content.data(), static_cast<long long>(content.size()));
    }

    void WriteFile(const string& filePath, const char* content, const size_t size)
    {
        ofstream file{ filePath, ios::out | ios::trunc | ios::binary };
        if (file.fail())
            return;
        file.write(content, static_cast<long long>(size));
        file.close();
    }
}