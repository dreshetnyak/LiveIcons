#pragma once
#include <span>
#include <string>
#include <Windows.h>
#include "StrLib.h"

namespace Utility
{
	using namespace std;

	string ToAbsolutePath(const string& currentFilePath, const string& relativeFilePath);
	string TrimPathExtension(const string& path);

	inline char FromHex(const char ch) noexcept
	{
		return static_cast<char>(ch >= '0' && ch <= '9'
			? ch - '0'
			: (ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : ch - 'a' + 10));
	}
	inline bool IsHex(const char ch) noexcept
	{
		return ch >= '0' && ch <= '9' || ch >= 'A' && ch <= 'F' || ch >= 'a' && ch <= 'f';
	}
    
	string UrlDecode(const string& urlEncoded);
	HRESULT ReadFile(const std::wstring& fileFullName, std::vector<char>& outFileContent) noexcept;
	HRESULT GetIStreamFileName(IStream* stream, wstring& outFileName) noexcept;
	HRESULT GetFileExtension(const wstring& fileName, wstring& outFileExtension) noexcept;
	HRESULT GetIStreamFileSize(IStream* stream, ULONGLONG& outSize) noexcept;
	HRESULT ReadIStream(IStream* stream, std::vector<char>& outFileContent) noexcept;
	HRESULT ReadIStream(IStream* stream, HANDLE outFileHandle) noexcept;
	HRESULT DecodeBase64(const string& base64Encoded, vector<char>& outDecoded) noexcept;
	HRESULT GetTempFileFullName(wstring& outTempFileName) noexcept;
	bool TryParseNumber(const string& numberStr, size_t& number) noexcept;
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
