#pragma once
#include <span>
#include <string>
#include <Windows.h>
#include "StrLib.h"

namespace Utility
{
	using namespace std;

	wstring GetLastErrorMessage();
	wstring GetLastErrorMessage(DWORD lastErrorCode);
	string ToAbsolutePath(const string& currentFilePath, const string& relativeFilePath);

	inline char FromHex(const char ch) { return static_cast<char>(isdigit(ch) ? ch - 0x30 : tolower(ch) - 'a' + 10); }
    inline bool IsHex(const char ch) { return ch > 47 && ch < 58 || ch > 64 && ch < 71 || ch > 96 && ch < 103; }
    
	string UrlDecode(const string& urlEncoded);
	HRESULT ReadFile(const std::wstring& fileFullName, std::vector<char>& outFileContent);
	HRESULT GetIStreamFileName(IStream* stream, wstring& outFileName);
	HRESULT GetFileExtension(const wstring& fileName, wstring& outFileExtension);
	HRESULT GetIStreamFileSize(IStream* stream, ULONGLONG& outSize);
	HRESULT GetIStreamFileNameAndSize(IStream* stream, ULONGLONG& outSize, wstring& outFileName);
	HRESULT SeekToBeginning(IStream* stream);
	HRESULT ReadIStream(IStream* stream, std::vector<char>& outFileContent);
	HRESULT ReadIStream(IStream* stream, char* outFileContent, size_t fileSize);
	HRESULT ReadIStream(IStream* stream, HANDLE outFileHandle);
	HRESULT DecodeBase64(const string& base64Encoded, vector<char>& outDecoded);
	HRESULT GetTempFileFullName(wstring& outTempFileName);
	bool TryParseNumber(const string& numberStr, size_t& number);
}

struct DataSpan final
{
	size_t Offset{};
	size_t Size{};

	DataSpan() = default;
	DataSpan(const DataSpan& other) = default;
	DataSpan(DataSpan&& other) noexcept : Offset{ other.Offset }, Size{ other.Size } { }
	DataSpan& operator=(const DataSpan& other)
	{
		if (this == &other)
			return *this;
		Offset = other.Offset;
		Size = other.Size;
		return *this;
	}
	DataSpan& operator=(DataSpan&& other) noexcept
	{
		if (this == &other)
			return *this;
		Offset = other.Offset;
		Size = other.Size;
		return *this;
	}
	~DataSpan() = default;
	[[nodiscard]] size_t OffsetAfterSpan() const { return Offset + Size; }
};

namespace Log
{
	using namespace std;

	void WriteToFile(const string& filePath, const string& message);
	void WriteFile(const string& filePath, const vector<char>& content);
	void WriteFile(const string& filePath, const char* content, size_t size);
	void Write(const string& message);
}