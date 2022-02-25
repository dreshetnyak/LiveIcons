#pragma once
#include <string>
#include <Windows.h>
#include "StrLib.h"

namespace Utility
{
	using namespace std;

	wstring GetLastErrorStr();
	string ToAbsolutePath(const string& currentFilePath, const string& relativeFilePath);

	inline char FromHex(const char ch) { return static_cast<char>(isdigit(ch) ? ch - 0x30 : tolower(ch) - 'a' + 10); }
    inline bool IsHex(const char ch) { return ch > 47 && ch < 58 || ch > 64 && ch < 71 || ch > 96 && ch < 103; }
    
	string UrlDecode(const string& urlEncoded);

	HRESULT ReadFile(const std::wstring& fileFullName, std::vector<char>& outFileContent);

	HRESULT GetIStreamFileExtension(IStream* stream, wstring& outFileExtension);
	HRESULT GetIStreamFileSize(IStream* stream, ULONGLONG& outSize);
	HRESULT SeekToBeginning(IStream* stream);
	HRESULT ReadIStream(IStream* stream, std::vector<char>& outFileContent);
}


namespace Log
{
	using namespace std;

	void WriteToFile(const string& filePath, const string& message);
	void Write(const string& message);
}