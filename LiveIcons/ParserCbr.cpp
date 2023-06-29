#include "pch.h"
#include "ParserCbr.h"
#include "Utility.h"
#include "Gfx.h"
#include "XmlDocument.h"

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
		vector<char> outImage{};
		const Xml::Document xmlFile{ string{fileContent.begin(), fileContent.end()} };

		HBITMAP coverBitmap{ nullptr };
		WTS_ALPHATYPE coverBitmapAlpha{};

		return GetFirstImage(xmlFile, coverBitmap, coverBitmapAlpha)
			? Result{{}, coverBitmap, coverBitmapAlpha }
			: Result{ E_FAIL };
	}

	bool Cbr::GetFirstImage(const Xml::Document& xmlFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const
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

	bool Cbr::GetCoverImage(const span<char>& base64EncodedImageSpan, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const
	{
		const string base64EncodedImage{ base64EncodedImageSpan.begin(), base64EncodedImageSpan.end() };
		StrLib::Filter<char>(base64EncodedImage, " \r\n\t");

		vector<char> image{};
		if (FAILED(Utility::DecodeBase64(base64EncodedImage, image)))
			return false;

		SIZE imageSize{};
		if (FAILED(Gfx::LoadImageToHBitmap(image.data(), image.size(), outBitmap, outAlphaType, imageSize)))
			return false;

		if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
			return true;

		DeleteObject(outBitmap);
		return false;
	}
}