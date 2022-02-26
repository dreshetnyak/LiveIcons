#include "pch.h"
#include "Utility.h"
#include "GlobalMem.h"

namespace Utility
{
    wstring GetLastErrorStr()
    {
        LPWSTR errorMessageBuffer = nullptr;
        if (!FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(),
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
            return ERROR_FILE_NOT_FOUND;
        outFileContent.reserve(fileStream.tellg());
        fileStream.seekg(0);
        std::copy(std::istream_iterator<char>(fileStream), std::istream_iterator<char>(), std::back_inserter(outFileContent));
        fileStream.close();
        return S_OK;
    }

    HRESULT GetIStreamFileName(IStream* stream, wstring& outFileName)
    {
        if (stream == nullptr)
            return ERROR_BAD_ARGUMENTS;
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
            return ERROR_BAD_ARGUMENTS;
        STATSTG streamStat{};
        if (const auto result = stream->Stat(&streamStat, STATFLAG_DEFAULT); FAILED(result))
            return result;
        outSize = streamStat.cbSize.QuadPart;
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
        
        if (const auto result = SeekToBeginning(stream); FAILED(result))
            return result;

        ULONG bytesRead{};
        if (const auto result = stream->Read(outFileContent.data(), static_cast<ULONG>(size), &bytesRead); FAILED(result))
            return result;

        static_cast<void>(SeekToBeginning(stream));
        return S_OK;
    }

    HRESULT DecodeBase64(const string& base64Encoded, vector<char>& outDecoded)
    {
        Log::Write("Utility::DecodeBase64: Starting.");

    	const auto encodedDataPtr = base64Encoded.data();
        const auto encodedSize = static_cast<DWORD>(base64Encoded.size());

        Log::Write(format("Utility::DecodeBase64: Encoded size: {}", encodedSize));

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

        Log::Write(format("Utility::DecodeBase64: Decoded size: {}", dwDecodedSize));
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
}

namespace Log
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
        ofstream file{ filePath, ios::out | ios::app | ios::binary };
        if (file.fail())
	        return;
        file.write(content.data(), content.size());
        file.close();
    }
}