#include "pch.h"
#include "ZipArchive.h"

namespace Zip
{
	Archive::Archive(IStream* fileStream) : ZipHandle(nullptr)
	{

	}

	Archive::~Archive()
	{
		if (ZipHandle != nullptr)
			unzClose(ZipHandle);
	}

	int Archive::Open()
	{
		if (ZipName.empty())
			return UNZ_PARAMERROR;

		zlib_filefunc64_def zlibFunctions;
		fill_win32_filefunc64W(&zlibFunctions);
		ZipHandle = unzOpen2_64(ZipName.c_str(), &zlibFunctions);
		if (ZipHandle == nullptr)
			return UNZ_BADZIPFILE;
		ZipFiles.reset(new Cache{ ZipHandle });
		return UNZ_OK;
	}

	const Position* Archive::Find(const function<bool(const string&)>& pathMatch, const size_t startIndex) const
	{
		for (const auto* pos = ZipFiles->At(startIndex); pos != nullptr; pos = ZipFiles->Next())
		{
			if (pathMatch(pos->FilePath))
				return pos;
		}

		return END_OF_LIST;
	}

	bool Archive::ReadMatching(string& outFilePath, vector<char>& content, const function<bool(const string&)>& pathMatch) const
	{
		const auto* filePosition = Find(pathMatch);
		if (filePosition == END_OF_LIST)
			return false;
		outFilePath = filePosition->FilePath;
		return ReadCurrent(content);
	}

	bool Archive::ReadPath(const string& filePath, vector<char>& content) const
	{
		const auto* filePosition = Find([&, filePath](const string& path) -> bool { return StrLib::EqualsCi(filePath, path); });
		return filePosition != END_OF_LIST
			? ReadCurrent(content)
			: false;
	}

	bool Archive::ReadCurrent(vector<char>& content) const
	{
		const auto* pos = ZipFiles->Current();
		if (pos == END_OF_LIST)
			return false;

		try
		{
			if (const auto responseCode = unzOpenCurrentFile(ZipHandle); responseCode != UNZ_OK)
				return responseCode;
		}
		catch (...)
		{ return false; }

		auto closeFile = [&]() -> int
		{
			try { return unzCloseCurrentFile(ZipHandle); }
			catch (...) { return UNZ_INTERNALERROR; }
		};

		int bytes_read;
		try
		{
			const auto uncompressedSize = pos->UncompressedSize;
			content.resize(uncompressedSize); // Caution, throws.
			bytes_read = unzReadCurrentFile(ZipHandle, content.data(), static_cast<unsigned>(uncompressedSize)); // Return the count of bytes read or an error code, error code is always negative
		}
		catch (...)
		{
			static_cast<void>(closeFile());
			return false;
		}
		
		return bytes_read >= 0
			? closeFile() == UNZ_OK
			: false;
	}
	
	wstring GetErrorMessage(const int errorCode)
	{
		switch (errorCode)
		{
		case UNZ_OK: return L"No error";
		case UNZ_PARAMERROR: return L"Parameter error";
		case UNZ_BADZIPFILE: return L"Bad zip file";
		case UNZ_INTERNALERROR: return L"Internal error";
		case UNZ_CRCERROR: return L"Checksum error";
		default: return L"Unknown error";
		}
	}
}