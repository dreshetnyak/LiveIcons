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

	std::shared_ptr<Result> Epub::Parse(const std::wstring& filePath)
	{
		ParsingContext epub{};

		epub.Zip.reset(new Zip::Archive{ filePath });
		auto& zip = *epub.Zip;
		if (int result; (result = zip.Open()) != UNZ_OK)
		{
			const auto error = std::format(L"Zip opening error: {}", Zip::GetErrorMessage(result));
			wcout << error << endl;
			return shared_ptr<Result>{ new Result{ error, {}, 0, {}, nullptr } };
		}

		vector<char> rootFileContentData;
		static_cast<void>(GetRootFileContent(epub, epub.RootFilePath, rootFileContentData));
		epub.RootFileXml.reset(new Xml::Document{ string{ rootFileContentData } });

		string title;
		if (!rootFileContentData.empty())
			epub.RootFileXml->GetElementContent("title", 0, title);

		// Load the image to CImage instance
		CImage cImage;
		string coverPath;
		vector<char> coverImage;
		if (!(GetCoverPath(epub, coverPath) && zip.ReadPath(coverPath, coverImage) && GetCoverFromImage(coverImage, cImage)) && 
			!GetCoverFromFirstImage(zip, coverPath, cImage))
			return shared_ptr<Result>{ new Result{ {}, StrLib::ToWstring(title), 0, {}, nullptr } };

		coverImage.clear();

		/*
		// Draw the image
		const auto bitmap = Gfx::ToBitmap(cImage, unique_ptr<SIZE>{ new SIZE{ 256, 256 } }.get());

		// TODO DEBUG CODE
		const auto fileNameOffset = StrLib::FindReverse(filePath, L'\\');
		const auto fileName = fileNameOffset != wstring::npos ? filePath.substr(fileNameOffset + 1) : filePath;
		Gfx::SaveImage(bitmap, L"R:\\Temp\\EPUBX\\" + fileName + L".png", Gfx::ImageFileType::Png);
		// TODO DEBUG CODE
*/

		return shared_ptr<Result>{ new Result{ {}, StrLib::ToWstring(title), 0, StrLib::ToWstring(coverPath), nullptr } };
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
		for (size_t offset = 0; rootFileXml.GetTag("item", offset, tag); offset += tag.size())
		{
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

	bool Epub::GetCoverFromFirstImage(const Zip::Archive& zip, string& outImagePath, CImage& outCImage)
	{
		vector<char> image;
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
			if (!zip.ReadCurrent(image) ||
				!Gfx::LoadImage(outCImage, image) ||
				!ImageSatisfiesCoverConstraints(outCImage))
				continue;

			outImagePath = imagePosition->FilePath;
			return true;
		}

		try	{ outCImage.Destroy(); }
		catch (...)	{ /* ignore */ }
		return false;
	}

	bool Epub::GetCoverFromImage(const vector<char>& image, CImage& outCImage)
	{
		if (Gfx::LoadImage(outCImage, image) && 
			ImageSatisfiesCoverConstraints(outCImage))
			return true;

		try { outCImage.Destroy(); }
		catch (...) { /* ignore */ }
		return false;
	}

	bool Epub::ImageSatisfiesCoverConstraints(const CImage& image)
	{
		const auto width = image.GetWidth();
		const auto height = image.GetHeight();
		return width > 1 && height > 1 && (height >= width || (width / height < 2)); // If the width is two times or more of height then it is likely not the cover but some other image.
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

		string rootFileTag;
		if (!containerXml.GetTag("rootfile", 0, rootFileTag))
			return false;

		string fullPath;
		if (!Xml::Document::GetTagAttribute(rootFileTag, "full-path", fullPath))
			return false;

		outRootFilePath = fullPath;
		return true;
	}
}