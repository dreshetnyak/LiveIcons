#pragma once
#include "ParserBase.h"
#include "XmlDocument.h"

namespace Parser
{
	class Fb2 final : public Base
	{
		Result Parse(const vector<char>& fileContent) const;
		bool GetImageFromCoverPage(const Xml::Document& xmlFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const;
		bool GetFirstImage(const Xml::Document& xmlFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const;
		bool GetCoverImage(const span<char>& base64EncodedImageSpan, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType) const;

	public:
		bool CanParse(const wstring& fileExtension) override;
		Result Parse(const wstring& fileName) override;
		Result Parse(IStream* stream) override;
	};
}