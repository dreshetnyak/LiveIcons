#pragma once
#include <filesystem>
#include <iostream>
#include <string>
#include <format>
#include "ParserBase.h"
#include "Utility.h"
#include "XmlDocument.h"
#include "ZipArchive.h"
#include "Gfx.h"

namespace Parser
{
	using namespace std;

	class Epub final : public Base
	{
		class ParsingContext
		{
		public:
			shared_ptr<Zip::Archive> Zip;
			shared_ptr<Xml::Document> RootFileXml;
			string RootFilePath;
		};

	public:
		Epub() = default;
		bool CanParse(const wstring& fileExtension) override { return StrLib::EqualsCi(fileExtension, wstring(L".epub")); }
		shared_ptr<Result> Parse(const wstring& filePath) override;

	private:
		static vector<string> PossibleRootFileLocations;
		static vector<string> ImageFileExtensions;
		static vector<string> HtmlFileExtensions;
		static vector<tuple<string, string, string>> XmlTagsThatMayContainCoverPath;
		static vector<tuple<string, string, string>> HtmlTagsThatMayContainCoverPath;

		bool GetCoverPath(const ParsingContext& epub, string& outCoverFilePath) const;
		bool GetCoverPathFromRootFile(const ParsingContext& epub, string& outCoverFilePath) const;
		bool GetCoverPathFromItemOrFile(const Zip::Archive& zip, const Xml::Document& rootFileXml, const string& rootFilePath, const string& tagIdOrFilePath, string& outCoverFilePath) const;
		bool GetImagePath(const Zip::Archive& zip, const string& currentPath, string& imagePath) const;
		bool GetCoverPathFromNcx(const ParsingContext& epub, string& outCoverImagePath) const;

		static bool GetCoverFromFirstImage(const Zip::Archive& zip, string& outImagePath, CImage& outCImage);
		static bool GetCoverFromImage(const vector<char>& image, CImage& outCImage);

		static bool ImageSatisfiesCoverConstraints(const CImage& image);
		static bool GetCoverPathFromHtml(const Zip::Archive& zip, const string& htmlPath, string& outCoverFilePath);
		static bool GetCoverPathTagIdFromMetaTag(const Xml::Document& rootFileXml, string& outCoverFilePath);
		static bool GetCoverPathFromMetaFile(const ParsingContext& epub, const string& coverMetaTagContent, string& outCoverFilePath);
		static bool GetCoverPathFromTagContainsCover(const Xml::Document& rootFileXml, string& outCoverFilePath);
		static bool GetCoverPathTagIdFromIdrefTag(const Xml::Document& rootFileXml, string& outTagIdOrFilePath);
		static bool GetRootFileContent(const ParsingContext& epub, string& outRootFilePath, vector<char>& outRootFileContent);
		static bool GetRootFilePathFromContainer(const ParsingContext& epub, string& outRootFilePath);
	};
}
