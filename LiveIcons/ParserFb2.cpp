#include "pch.h"
#include "ParserFb2.h"
#include "Utility.h"
#include "Gfx.h"
#include "XmlDocument.h"

namespace Parser
{
	bool Fb2::CanParse(const wstring& fileExtension)
	{
		return StrLib::EqualsCi(fileExtension, wstring{ L".fb2" });
	}

	Result Fb2::Parse(const wstring& fileName)
	{
		vector<char> fileContent{};
		const auto result = Utility::ReadFile(fileName, fileContent);
		return SUCCEEDED(result)
			? Parse(fileContent)
			: Result{ result };
	}

	Result Fb2::Parse(IStream* stream)
	{
		vector<char> fileContent{};
		const auto result = Utility::ReadIStream(stream, fileContent);
		return SUCCEEDED(result)
			? Parse(fileContent)
			: Result{ result };
	}
	
	Result Fb2::Parse(const vector<char>& fileContent) const
	{
		vector<char> outImage{};
		const Xml::Document xmlFile{ string{fileContent} };

		HBITMAP coverBitmap{ nullptr };
		WTS_ALPHATYPE coverBitmapAlpha{};

		return GetImageFromCoverPage(xmlFile, coverBitmap, coverBitmapAlpha) || GetFirstImage(xmlFile, coverBitmap, coverBitmapAlpha)
			? Result{{}, coverBitmap, coverBitmapAlpha }
			: Result{ E_FAIL };
	}

	bool Fb2::GetImageFromCoverPage(const Xml::Document& xmlFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const
	{
		string coverPageElementContentStr;
		if (!xmlFile.GetElementContent("coverpage", 0, coverPageElementContentStr) || coverPageElementContentStr.empty())
			return false;

		string imageTag{};
		if (const Xml::Document coverPageElementContent{ coverPageElementContentStr }; !coverPageElementContent.GetTag("image", 0, imageTag))
			return false;

		string coverHref{};		
		if (!Xml::Document::GetTagAttribute(imageTag, "href", coverHref) || coverHref.empty())
			return false;

		if (coverHref[0] == '#')
		{
			if (coverHref.size() == 1)
				return false;
			coverHref.erase(0, 1);
		}

		DataSpan elementDataSpan{};
		span<char> base64EncodedImageSpan{};
		return xmlFile.GetElementTagContainsContentPos("binary", format("id=\"{}\"", coverHref), 0, base64EncodedImageSpan, elementDataSpan) &&
			GetCoverImage(base64EncodedImageSpan, outBitmap, outAlphaType);
	}

	bool Fb2::GetFirstImage(const Xml::Document& xmlFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const
	{
		DataSpan elementDataSpan{};
		span<char> base64EncodedImageSpan{};
		size_t startOffset{ 0 };
		while (xmlFile.GetElementTagContainsContentPos("binary", "image/", startOffset, base64EncodedImageSpan, elementDataSpan))
		{
			if (GetCoverImage(base64EncodedImageSpan, outBitmap, outAlphaType))
				return true;
			startOffset = elementDataSpan.OffsetAfterSpan();
		}

		return false;
	}

	bool Fb2::GetCoverImage(const span<char>& base64EncodedImageSpan, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const
	{
		const string base64EncodedImage{ base64EncodedImageSpan };
		StrLib::Filter<char>(base64EncodedImage, " \r\n\t");

		vector<char> image{};
		if (FAILED(Utility::DecodeBase64(base64EncodedImage, image)))
			return false;

		SIZE imageSize{};
		if (FAILED(Gfx::LoadImageToHBitmap(image, outBitmap, outAlphaType, imageSize)))
			return false;

		if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
			return true;

		DeleteObject(outBitmap);
		return false;
	}
}
