#include "pch.h"
#include "ParserFb2.h"
#include "Utility.h"

#include <fstream>

#include "XmlDocument.h"

namespace Parser
{
	bool ParserFb2::CanParse(const wstring& fileExtension)
	{
		return StrLib::EqualsCi(fileExtension, wstring{ L".fb2" });
	}

	Result ParserFb2::Parse(const wstring& fileName)
	{
		vector<char> fileContent{};
		const auto result = Utility::ReadFile(fileName, fileContent);
		return SUCCEEDED(result)
			? Parse(fileContent)
			: Result{ result };
	}

	Result ParserFb2::Parse(IStream* stream)
	{
		vector<char> fileContent{};
		const auto result = Utility::ReadIStream(stream, fileContent);
		return SUCCEEDED(result)
			? Parse(fileContent)
			: Result{ result };
	}

	
	Result ParserFb2::Parse(const vector<char>& fileContent)
	{
		Xml::Document xmlFile{ string{fileContent} };
		if (GetImageFromCoverPage(xmlFile))
			return Result{};

		//TODO return first image

		return Result{};
	}

	bool ParserFb2::GetImageFromCoverPage(const Xml::Document& xmlFile)
	{
		string coverPageElementContentStr;
		if (!xmlFile.GetElementContent("coverpage", 0, coverPageElementContentStr) || coverPageElementContentStr.empty())
			return false;

		string coverHref{};		
		if (!Xml::Document::GetTagAttribute(coverPageElementContentStr, "href", coverHref) || coverHref.empty())
			return false;

		if (coverHref[0] == '#')
		{
			if (coverHref.size() == 1)
				return false;
			coverHref.erase(0);
		}

		xmlFile.GetTagThatContains("binary", coverHref, )
		

	}


}
