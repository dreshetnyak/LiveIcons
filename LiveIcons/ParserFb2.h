#pragma once
#include "ParserBase.h"
#include "XmlDocument.h"

namespace Parser
{
	class ParserFb2 final : public Base
	{
		Result Parse(const vector<char>& fileContent);
		bool GetImageFromCoverPage(const Xml::Document& xmlFile);
	public:
		bool CanParse(const wstring& fileExtension) override;
		Result Parse(const wstring& fileName) override;
		Result Parse(IStream* stream) override;
	};
}