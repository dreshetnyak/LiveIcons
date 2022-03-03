#include "pch.h"
#include "ParserEpub.h"

namespace Parser
{
	vector<string> Epub::PossibleRootFileLocations
	{
		"OEBPS/content.opf",
		"OPS/content.opf",
		"content.opf",
		"OEBPS/package.opf",
		"OPS/package.opf",
		"package.opf",
		"book.opf",
		"OEBPS/opf.opf"
	};

	vector<string> Epub::ImageFileExtensions
	{
		".bmp",
		".ico",
		".gif",
		".jpg",
		".jpe",
		".jfif",
		".jpeg",
		".png",
		".tif",
		".tiff"
	};

	vector<string> Epub::HtmlFileExtensions
	{
		".html",
		".xhtml",
		".xml",
		".htm"
	};

	vector<tuple<string, string, string>> Epub::XmlTagsThatMayContainCoverPath
	{	//item1: tag name; item2: contained string; item3: attribute that has the path
		{"item", "cover", "href"},
		{"reference", "\"cover\"", "href"},
		{"item", "id=\"cvi\"", "href"},	//Points to an html
		{"reference", "\"title-page\"", "href"} //Points to an html
	};

	vector<tuple<string, string, string>> Epub::HtmlTagsThatMayContainCoverPath
	{	//item1: tag name; item2: contained string; item3: attribute that has the path
		{"image", "href=", "href"},
		{"img", "src=", "src"},
	};

	bool Epub::CanParse(const wstring& fileExtension)
	{
		return StrLib::EqualsCi(fileExtension, wstring{ L".epub" });
	}

	Result Epub::Parse(IStream* stream)
	{
		ParsingContext epub{};
		epub.Zip.reset(new Zip::Archive{ stream });
		return Parse(epub);
	}

	Result Epub::Parse(const std::wstring& filePath)
	{
		ParsingContext epub{};
		epub.Zip.reset(new Zip::Archive{ filePath });
		return Parse(epub);
	}

	Result Epub::Parse(ParsingContext& epub) const
	{
		auto& zip = *epub.Zip;
		if (int result; (result = zip.Open()) != UNZ_OK)
			return Result{ ERROR_CANT_ACCESS_FILE, std::format(L"Zip opening error: {}", Zip::GetErrorMessage(result)) };

		vector<char> rootFileContentData;
		static_cast<void>(GetRootFileContent(epub, epub.RootFilePath, rootFileContentData));
		epub.RootFileXml.reset(new Xml::Document{ string{ rootFileContentData } });
		string title;
		if (!rootFileContentData.empty())
			epub.RootFileXml->GetElementContent("title", 0, title);

		string coverPath{};
		vector<char> coverImageBytes{};
		HBITMAP coverBitmap{ nullptr };
		WTS_ALPHATYPE coverBitmapAlpha{};
		
		if (GetCoverPath(epub, coverPath) && zip.ReadPath(coverPath, coverImageBytes) && GetCoverBitmap(coverImageBytes, coverBitmap, coverBitmapAlpha) == S_OK)
			return Result{ StrLib::ToWstring(title), coverBitmap, coverBitmapAlpha };

		return GetCoverFromFirstImage(zip, coverBitmap, coverBitmapAlpha)
			? Result{ StrLib::ToWstring(title), coverBitmap, coverBitmapAlpha }
			: Result{ E_FAIL, L"Unable to parse the EPUB file" };
	}

	bool Epub::GetCoverPath(const ParsingContext& epub, string& outCoverFilePath) const
	{
		return GetCoverPathFromRootFile(epub, outCoverFilePath) || 
			GetCoverPathFromNcx(epub, outCoverFilePath);
	}

	bool Epub::GetCoverPathFromRootFile(const ParsingContext& epub, string& outCoverFilePath) const
	{
		const auto& rootFileXml = *epub.RootFileXml;
		const auto& rootFilePath = epub.RootFilePath;
		if (rootFileXml.Empty())
			return false;

		string path, tagIdOrFilePath;
		const auto& zip = *epub.Zip;

		// Get 'content' attribute value of the meta tag with id 'cover'.
		if (GetCoverPathTagIdFromMetaTag(rootFileXml, tagIdOrFilePath) && 
			GetCoverPathFromItemOrFile(zip, rootFileXml, rootFilePath, tagIdOrFilePath, path))
		{
			outCoverFilePath = path;
			return true;
		}

		// Try to locate the cover path tags in the root file in the common places
		for (const auto& [tag_name, contains_str, path_attribute_name] : XmlTagsThatMayContainCoverPath)
		{
			if (!rootFileXml.GetTagAttributeValue(tag_name, contains_str, path_attribute_name, path) ||
				!GetImagePath(zip, rootFilePath, path))
				continue;			
			outCoverFilePath = path;
			return true;
		}

		// Try to find any item that has id or href that contains 'cover' word.		
		if (GetCoverPathFromTagContainsCover(rootFileXml, path) &&
			GetImagePath(zip, rootFilePath, path))
		{
			outCoverFilePath = path;
			return true;
		}

		// Get 'idref' attribute value of the first tag 'itemref' in the spine.
		if (GetCoverPathTagIdFromIdrefTag(rootFileXml, tagIdOrFilePath) &&
			GetCoverPathFromItemOrFile(zip, rootFileXml, rootFilePath, tagIdOrFilePath, path))
		{
			outCoverFilePath = path;
			return true;
		}

		return false;
	}

	bool Epub::GetCoverPathFromItemOrFile(const Zip::Archive& zip, const Xml::Document& rootFileXml, const string& rootFilePath, const string& tagIdOrFilePath, string& outCoverFilePath) const
	{
		string path;

		// Assume that the 'content' attribute value is an id of the 'item' tag containing cover in href (most of the cases)
		if (rootFileXml.GetTagAttributeValue("item", std::format("id=\"{}\"", tagIdOrFilePath), "href", path) && GetImagePath(zip, rootFilePath, path))
		{
			outCoverFilePath = path;
			return true;
		}

		// Assume that the 'content' attribute value contains the path to the image (rare)
		path = tagIdOrFilePath;
		if (!GetImagePath(zip, rootFilePath, path))
			return false;

		outCoverFilePath = path;
		return true;
	}

	bool Epub::GetCoverPathFromTagContainsCover(const Xml::Document& rootFileXml, string& outCoverFilePath)
	{
		string tag, hrefValue, idValue;
		for (size_t offset = 0, tagOffset = 0; rootFileXml.GetTag("item", offset, tagOffset, tag); offset += tag.size())
		{
			offset = tagOffset;
			if (!Xml::Document::GetTagAttribute(tag, "href", hrefValue) ||
				StrLib::FindCi(hrefValue, string{ "cover" }, 0) == string::npos &&
				(!Xml::Document::GetTagAttribute(tag, "id", idValue) || StrLib::FindCi(idValue, string{ "cover" }, 0) == string::npos))
				continue;

			outCoverFilePath = hrefValue;
			return true;
		}

		return false;
	}

	bool Epub::GetImagePath(const Zip::Archive& zip, const string& currentPath, string& imagePath) const
	{
		auto absoluteImagePath = Utility::ToAbsolutePath(currentPath, imagePath);
		if (StrLib::EndsWith(absoluteImagePath, HtmlFileExtensions))
		{
			if (!GetCoverPathFromHtml(zip, absoluteImagePath, absoluteImagePath))
				return false;
		}
		else if (!StrLib::EndsWith(absoluteImagePath, ImageFileExtensions) || !zip.FileExists(absoluteImagePath))
			return false;

		imagePath = absoluteImagePath;
		return true;
	}

	bool Epub::GetCoverPathFromHtml(const Zip::Archive& zip, const string& htmlPath, string& outCoverFilePath)
	{
		vector<char> htmlFileContent;
		if (!zip.ReadPath(htmlPath, htmlFileContent))
			return false;
		const auto html{ Xml::Document{string{htmlFileContent}} };

		string newPath;
		for (const auto& [tagName, containsStr, pathAttributeName] : HtmlTagsThatMayContainCoverPath)
		{
			if (!html.GetTagAttributeValue(tagName, containsStr, pathAttributeName, newPath) ||
				!StrLib::EndsWith(newPath, ImageFileExtensions))
				continue;

			newPath = Utility::ToAbsolutePath(htmlPath, newPath);
			if (!zip.FileExists(newPath))
				continue;

			outCoverFilePath = newPath;
			return true;
		}

		return false;
	}

	bool Epub::GetCoverPathFromNcx(const ParsingContext& epub, string& outCoverImagePath) const
	{
		const auto& zip = *epub.Zip;
		const auto filePosition = zip.Find([&](const string& path) -> bool { return StrLib::EndsWith(path, static_cast<const basic_string<char>>(".ncx")); });
		if (filePosition == END_OF_LIST)
			return false;
			
		vector<char> ncxContent;
		if (!zip.ReadCurrent(ncxContent))
			return false;

		const auto ncx = Xml::Document{ string{ncxContent} };

		string firstNavPoint;
		if (!ncx.GetElementContent("navPoint", 0, firstNavPoint))
			return false;

		const auto navPoint = Xml::Document{ firstNavPoint };
		if (string navPointText;
			!navPoint.GetElementContent("text", 0, navPointText) &&
			StrLib::FindCi<char>(navPointText, "cover", 0) == std::string::npos)
			return false;

		string navPointPath;
		if (!navPoint.GetTagAttributeValue("content", "src=", "src", navPointPath))
			return false;

		navPointPath = Utility::UrlDecode(navPointPath);
		if (const auto anchorOffset = StrLib::FindReverse(navPointPath, '#', navPointPath.size() - 1); anchorOffset != string::npos)
			navPointPath.erase(anchorOffset); // The path can be with the anchor reference, example: "index_split_000.html#17"

		if (!GetImagePath(zip, filePosition->FilePath, navPointPath))
			return false;

		outCoverImagePath = navPointPath;
		return true;
	}

	bool Epub::GetCoverFromFirstImage(const Zip::Archive& zip, HBITMAP& coverBitmap, WTS_ALPHATYPE& coverBitmapAlpha)
	{
		SIZE imageSize{ 0, 0 };
		vector<char> imageFileData;
		vector<const Zip::Position*> images;
		const Zip::Position* filePosition = nullptr;
		for (size_t fileIndex = 0, positionIndex = 0; 
			fileIndex < 5 && (filePosition = zip.Find([&](const string& path) -> bool { return StrLib::EndsWith(path, ImageFileExtensions); }, positionIndex)) != nullptr;
			positionIndex = static_cast<size_t>(filePosition->FileIndex) + 1, ++fileIndex)
		{
			images.push_back(filePosition);
		}

		if (images.empty())
			return false;

		ranges::sort(images, [](const Zip::Position* a, const Zip::Position* b) -> bool { return a->FilePath < b->FilePath; });
		for (const auto imagePosition : images)
		{
			zip.SetCurrent(*imagePosition);
			if (!zip.ReadCurrent(imageFileData) || Gfx::LoadImageToHBitmap(imageFileData.data(), imageFileData.size(), coverBitmap, coverBitmapAlpha, imageSize) != S_OK )
				continue;
			if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
				return true;
			
			DeleteObject(coverBitmap);
		}

		return false;
	}

	HRESULT Epub::GetCoverBitmap(const vector<char>& imageFileData, HBITMAP& coverBitmap, WTS_ALPHATYPE& coverBitmapAlpha)
	{
		SIZE imageSize{ 0, 0 };
		if (const auto result = Gfx::LoadImageToHBitmap(imageFileData.data(), imageFileData.size(), coverBitmap, coverBitmapAlpha, imageSize); FAILED(result))
			return result;
		if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
			return S_OK;
		DeleteObject(coverBitmap);
		return S_FALSE;
	}

	bool Epub::GetCoverPathTagIdFromMetaTag(const Xml::Document& rootFileXml, string& outCoverFilePath)
	{
		string coverTag;
		return rootFileXml.GetTagThatContains("meta", "name=\"cover\"", coverTag) &&
			Xml::Document::GetTagAttribute(coverTag, "content", outCoverFilePath);
	}

	bool Epub::GetCoverPathFromMetaFile(const ParsingContext& epub, const string& coverMetaTagContent, string& outCoverFilePath)
	{
		if (!StrLib::EndsWith(coverMetaTagContent, ImageFileExtensions) || 
			!epub.Zip->FileExists(coverMetaTagContent))
			return false;
		outCoverFilePath = coverMetaTagContent;
		return true;
	}

	bool Epub::GetCoverPathTagIdFromIdrefTag(const Xml::Document& rootFileXml, string& outTagIdOrFilePath)
	{
		string tag;
		return rootFileXml.GetTagThatContains("itemref", "idref=", tag) &&
			Xml::Document::GetTagAttribute(tag, "idref", outTagIdOrFilePath);
	}

	bool Epub::GetRootFileContent(const ParsingContext& epub, string& outRootFilePath, vector<char>& outRootFileContent)
	{
		const auto& zip = *epub.Zip;
		return GetRootFilePathFromContainer(epub, outRootFilePath) && zip.ReadPath(outRootFilePath, outRootFileContent) ||
			zip.ReadMatching(outRootFilePath, outRootFileContent, [&](const string& path) -> bool { return StrLib::EqualsCiOneOf(path, PossibleRootFileLocations); }) ||
			zip.ReadMatching(outRootFilePath, outRootFileContent, [&](const string& path) -> bool { return StrLib::EndsWith(path, static_cast<const basic_string<char>>(".opf")); });
	}

	bool Epub::GetRootFilePathFromContainer(const ParsingContext& epub, string& outRootFilePath)
	{
		vector<char> fileContent;
		if (!epub.Zip->ReadPath("META-INF/container.xml", fileContent))
			return false;

		const auto containerXml = Xml::Document{ string{ fileContent } };

		size_t tagOffset{};
		string rootFileTag;
		if (!containerXml.GetTag("rootfile", 0, tagOffset, rootFileTag))
			return false;

		string fullPath;
		if (!Xml::Document::GetTagAttribute(rootFileTag, "full-path", fullPath))
			return false;

		outRootFilePath = fullPath;
		return true;
	}
}