#include "pch.h"
#include "Utility.h"

#include <fstream>

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
}

namespace Log
{
    void Write(const string& message)
    {
        //OutputDebugString();
        WriteToFile("C:\\Projects\\LiveIcons\\x64\\Debug\\log.txt", format("{:%Y-%m-%d %T} {}", static_cast<chrono::sys_time<chrono::nanoseconds>>(chrono::system_clock::now()), message).c_str());
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

}