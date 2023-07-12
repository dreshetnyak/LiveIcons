#include "pch.h"
#include "ParserCbr.h"

namespace Parser
{
	bool Cbr::CanParse(const wstring& fileExtension)
	{
		return StrLib::EqualsCi(fileExtension, wstring{ L".cbr" });
	}

	Result Cbr::Parse(const wstring& fileName)
	{
		vector<char> fileContent{};
		const auto result = Utility::ReadFile(fileName, fileContent);
		return SUCCEEDED(result)
			? Parse(fileContent)
			: Result{ result };
	}

	Result Cbr::Parse(IStream* stream)
	{
		vector<char> fileContent{};
		const auto result = Utility::ReadIStream(stream, fileContent);
		return SUCCEEDED(result)
			? Parse(fileContent)
			: Result{ result };
	}

	Result Cbr::Parse(const vector<char>& fileContent) const
	{
		RAROpenArchiveDataEx archiveData{};
		memset(&archiveData, 0, sizeof RAROpenArchiveDataEx);
		archiveData.ArcName = const_cast<char *>("archive"); // Dummy name
		archiveData.OpenMode = RAR_OM_LIST;
		archiveData.Callback = nullptr; //TODO What is the callback for, how does it work? UNRARCALLBACK

		const auto handle = RAROpenArchiveEx(&archiveData);
		if (handle == nullptr)
			return Result{ E_FAIL };

		const auto dataSet = static_cast<DataSet*>(handle); // The handle is actually DataSet*, it contains Archive Arc, Archive is custom derived from FileVector which will be used as a source of the file data
		dataSet->Arc.SetFileContent(fileContent);			// fileContent contains the bytes of the file that we need to unpack.

		//TODO Continue here, Rar functionality must be extracted to a separate class

		auto arcNames = dataSet->Cmd.ArcNames;
		auto itemsCount = arcNames.ItemsCount();

		shared_ptr<wchar_t[]> buffer { new wchar_t[1024] };
		arcNames.GetString(buffer.get(), 1024);
		arcNames.GetString(buffer.get(), 1024);

		HBITMAP coverBitmap{ nullptr };
		WTS_ALPHATYPE coverBitmapAlpha{};

		//return GetFirstImage(xmlFile, coverBitmap, coverBitmapAlpha)
		//	? Result{{}, coverBitmap, coverBitmapAlpha }
		//	: Result{ E_FAIL };

		return Result{ S_OK };
	}

	//bool Cbr::GetFirstImage(const Xml::Document& xmlFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const
	//{
	//	DataSpan elementDataSpan{};
	//	span<char> base64EncodedImageSpan{};
	//	size_t startOffset{ 0 };
	//	while (xmlFile.GetElementTagContainsContentPos("binary", "image/", startOffset, base64EncodedImageSpan, elementDataSpan))
	//	{
	//		if (GetCoverImage(base64EncodedImageSpan, outBitmap, outAlphaType))
	//			return true;
	//		startOffset = elementDataSpan.OffsetAfterSpan();
	//	}

	//	return false;
	//}

	//bool Cbr::GetCoverImage(const span<char>& base64EncodedImageSpan, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const
	//{
	//	const string base64EncodedImage{ base64EncodedImageSpan.begin(), base64EncodedImageSpan.end() };
	//	StrLib::Filter<char>(base64EncodedImage, " \r\n\t");

	//	vector<char> image{};
	//	if (FAILED(Utility::DecodeBase64(base64EncodedImage, image)))
	//		return false;

	//	SIZE imageSize{};
	//	if (FAILED(Gfx::LoadImageToHBitmap(image.data(), image.size(), outBitmap, outAlphaType, imageSize)))
	//		return false;

	//	if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
	//		return true;

	//	DeleteObject(outBitmap);
	//	return false;
	//}
}
