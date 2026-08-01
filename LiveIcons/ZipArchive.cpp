#include "pch.h"
#include "ZipArchive.h"
#include "ZipStream.h"

namespace
{
	constexpr std::uint64_t MaximumUncompressedMemberSize{ 64ULL * 1024ULL * 1024ULL };

	class CurrentZipFile final
	{
		unzFile Archive{};
		bool IsOpen{};

	public:
		explicit CurrentZipFile(const unzFile archive) noexcept : Archive{ archive } { }
		CurrentZipFile(const CurrentZipFile&) = delete;
		CurrentZipFile& operator=(const CurrentZipFile&) = delete;

		~CurrentZipFile() noexcept
		{
			static_cast<void>(Close());
		}

		[[nodiscard]] int Open() noexcept
		{
			try
			{
				const int result = unzOpenCurrentFile(Archive);
				IsOpen = result == UNZ_OK;
				return result;
			}
			catch (...)
			{
				return UNZ_INTERNALERROR;
			}
		}

		[[nodiscard]] int Close() noexcept
		{
			if (!IsOpen)
				return UNZ_OK;
			IsOpen = false;
			try
			{
				return unzCloseCurrentFile(Archive);
			}
			catch (...)
			{
				return UNZ_INTERNALERROR;
			}
		}
	};
}

namespace Zip
{
	Archive::Archive(wstring fileName) : ZipName(move(fileName))
	{
		fill_win32_filefunc64W(&ZlibFunctions);		
	}

	Archive::Archive(IStream* fileStream) : FileStream(fileStream)
	{
		ZipStream::SetIStreamHandlers(&ZlibFunctions);		
	}

	Archive::~Archive()
	{
		if (ZipHandle != nullptr)
			unzClose(ZipHandle);
	}

	int Archive::Open()
	{
		if (ZipHandle != nullptr)
			return UNZ_PARAMERROR;

		if (!ZipName.empty())
			ZipHandle = unzOpen2_64(ZipName.c_str(), &ZlibFunctions);
		else if (FileStream != nullptr)
			ZipHandle = unzOpen2_64(FileStream, &ZlibFunctions);
		else
			return UNZ_PARAMERROR;		
		
		if (ZipHandle == nullptr)
			return UNZ_BADZIPFILE;
		ZipFiles.reset(new Cache{ ZipHandle });
		return UNZ_OK;
	}

	const Position* Archive::Find(const function<bool(const string&)>& pathMatch, const size_t startIndex) const
	{
		if (ZipFiles == nullptr)
			return END_OF_LIST;

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
		content.clear();
		if (ZipFiles == nullptr || ZipHandle == nullptr)
			return false;

		const auto* pos = ZipFiles->Current();
		if (pos == END_OF_LIST)
			return false;

		const auto uncompressedSize = pos->UncompressedSize;
		if (uncompressedSize > MaximumUncompressedMemberSize ||
			uncompressedSize > content.max_size())
			return false;

		CurrentZipFile currentFile{ ZipHandle };
		if (currentFile.Open() != UNZ_OK)
			return false;

		try
		{
			content.resize(static_cast<size_t>(uncompressedSize));
		}
		catch (...)
		{
			return false;
		}

		size_t totalRead{};
		while (totalRead < content.size())
		{
			const size_t remaining = content.size() - totalRead;
			const auto requestSize = static_cast<unsigned>((std::min)(
				remaining, static_cast<size_t>((std::numeric_limits<unsigned>::max)())));

			int bytesRead;
			try
			{
				bytesRead = unzReadCurrentFile(
					ZipHandle, content.data() + totalRead, requestSize);
			}
			catch (...)
			{
				content.clear();
				return false;
			}

			if (bytesRead <= 0)
			{
				content.clear();
				return false;
			}
			if (static_cast<unsigned>(bytesRead) > requestSize)
			{
				content.clear();
				return false;
			}
			totalRead += static_cast<size_t>(bytesRead);
		}

		if (currentFile.Close() != UNZ_OK)
		{
			content.clear();
			return false;
		}
		return true;
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
