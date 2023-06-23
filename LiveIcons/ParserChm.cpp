#include "pch.h"
#include "ParserChm.h"
#include "Utility.h"
#include "Gfx.h"
#include "XmlDocument.h"

namespace Parser
{
    vector<string> Chm::ImageFileExtensions
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

    vector<string> Chm::HtmlExtensions
    {
        ".html",
        ".htm",
        ".shtml"
    };

    vector<string> Chm::TocFileNames
    {
        "toc.html",
        "toc.htm"
    };

    vector<string> Chm::OtherTocFileNames
    {
        "cover.html",
        "cover.htm",
	    "content.html",
	    "content.htm",
        "index.html",
        "index.htm",
        "start.html",
        "start.htm"
    };

    vector<string> Chm::CoverImageFileEnds
    {
        "_xs",
        "_cover",
        "/cover",
        "/cover_01"
    };

    vector<string> Chm::CoverImageFileNotEnds
    {
        "next",
        "previous",
        "top",
        "bottom",
        "_logo"
    };

    vector<string> Chm::CoverImageFileContains
    {
        "cover"
    };

	bool Chm::CanParse(const wstring& fileExtension)
	{
		return StrLib::EqualsCi(fileExtension, wstring{ L".chm" });
	}

	Result Chm::Parse(const wstring& fileName)
	{
        IStream* stream{ nullptr };
        if (const auto result = SHCreateStreamOnFileW(fileName.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, &stream); FAILED(result))
            return Result{ result };
        const auto result = Parse(stream);
        stream->Release();
        return Result{ result };
	}

	Result Chm::Parse(IStream* stream)
	{
        HBITMAP bitmap{ nullptr };
        WTS_ALPHATYPE alphaType{};
        return TryGetCoverBitmap(stream, bitmap, alphaType)
			? Result{ L"", bitmap, alphaType }
			: Result{ E_FAIL };
	}

    bool Chm::TryGetCoverBitmap(IStream* stream, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        chm_file chmFile;
        IStreamReaderCtx ctx{ stream };
        if (!chm_parse(&chmFile, IStreamReader, &ctx))
            return false;

        // Find an image file with the name that ends with _xs, /cover or /cover_01 those files are usually the cover images
        if (TryGetCoverFromXsFile(chmFile, outBitmap, outAlphaType))
            return true;

        // Locate toc.html/toc.htm file get the first image that looks like a cover
        if (TryGetCoverFromToc(chmFile, outBitmap, outAlphaType))
            return true;

        // Read HHC objects and check if there is an object with a cover, load referenced cover file
        if (TryGetCoverFromHhc(chmFile, outBitmap, outAlphaType))
            return true;

        // Try to get image that contains 'cover'
        if (TryGetCoverByFileName(chmFile, outBitmap, outAlphaType))
            return true;

        // Get the first HTML and get the first matching image from it
        if (TryGetCoverFromFirstHtml(chmFile, outBitmap, outAlphaType))
            return true;

        return false;
    }

    bool Chm::TryGetCoverFromXsFile(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        return TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& path) -> bool
        {
            if (!StrLib::EndsWith(path, ImageFileExtensions))
                return false;
            const auto pathWithoutExtension = Utility::TrimPathExtension(path);
            return !StrLib::EndsWith(pathWithoutExtension, CoverImageFileNotEnds) && StrLib::EndsWith(pathWithoutExtension, CoverImageFileEnds);
        });
    }

    bool Chm::TryGetCoverFromToc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        return TryGetCoverFromHtml(chmFile, TocFileNames, outBitmap, outAlphaType) ||
            TryGetCoverFromHtml(chmFile, OtherTocFileNames, outBitmap, outAlphaType);
    }

    bool Chm::TryGetCoverFromHhc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        const string hhcExtension{ ".hhc" };
        vector<char> hhcFileContent{};
        for (int fileIndex{ 0 }; TryGetFileEndsWithContent(chmFile, fileIndex, hhcFileContent, hhcExtension); ++fileIndex)
        {
            const Xml::Document hhcXml{ string {hhcFileContent.begin(), hhcFileContent.end()} };
            hhcFileContent.clear();
            if (TryGetPathFromHhcObjects(chmFile, hhcXml, outBitmap, outAlphaType))
                return true;
        }

        return false;
    }

    bool Chm::TryGetPathFromHhcObjects(chm_file& chmFile, const Xml::Document& hhcXml, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        auto isParamTagIndicatesNameObject = [](const string& paramTag) -> bool
        {
            string attributeValue{};
            return Xml::Document::GetTagAttribute(paramTag, "name", attributeValue) &&
                StrLib::EqualsCi(attributeValue, string{ "Name" });
        };

        auto isParamTagIndicatesCoverObject = [](const string& paramTag) -> bool
        {
            string attributeValue{};
            return Xml::Document::GetTagAttribute(paramTag, "name", attributeValue) &&
                StrLib::EqualsCi(attributeValue, string{ "Name" }) &&
                Xml::Document::GetTagAttribute(paramTag, "value", attributeValue) &&
                StrLib::EqualsCi(attributeValue, string{ "Cover" });
        };

        auto isParamTagWithCoverPath = [](const string& paramTag) -> bool
        {
            string attributeValue{};
            return Xml::Document::GetTagAttribute(paramTag, "name", attributeValue) &&
                StrLib::EqualsCi(attributeValue, string{ "Local" });
        };

        string elementContent;
        const string objectElementName{ "object" };
        for (size_t elementSearchOffset = 0, contentOffset = 0, count = 0; hhcXml.GetElementContent("object", elementSearchOffset, elementContent, &contentOffset); elementSearchOffset += elementContent.size(), ++count)
        {
            elementSearchOffset = contentOffset;
            const Xml::Document hhcObjectXml{ string {elementContent} }; //CAUTION! Do not inline! Possible bug in VS2022.
            if (TryGetPathFromHhcObject(chmFile, hhcObjectXml, count < 2 ? isParamTagIndicatesNameObject : isParamTagIndicatesCoverObject, isParamTagWithCoverPath, outBitmap, outAlphaType))
                return true;
        }

        return false;
    }

    bool Chm::TryGetPathFromHhcObject(chm_file& chmFile, const Xml::Document& hhcObjectXml,
        const function<bool(const string&)>& isParamTagIndicatesCoverObject,
        const function<bool(const string&)>& isParamTagWithCoverPath,
        HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        string path, tag;
        vector<char> coverFileContent{};

        if (!hhcObjectXml.ContainsTag("param", isParamTagIndicatesCoverObject) ||
            !hhcObjectXml.GetTag("param", tag, isParamTagWithCoverPath) ||
            !Xml::Document::GetTagAttribute(tag, "value", path))
            return false;

        PreparePath(path);

        if (StrLib::EndsWith(path, HtmlExtensions))
            return TryGetCoverByHtmlPath(chmFile, path, outBitmap, outAlphaType);

        if (StrLib::EndsWith(path, ImageFileExtensions))
            return TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& filePath) -> bool { return StrLib::EndsWith(filePath, path); });

        return false;
    }

    bool Chm::TryGetCoverByFileName(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        return TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& path) -> bool
        {
            if (!StrLib::EndsWith(path, ImageFileExtensions))
                return false;
            const auto pathWithoutExtension = Utility::TrimPathExtension(path);
            return !StrLib::EndsWith(pathWithoutExtension, CoverImageFileNotEnds) && StrLib::ContainsCiOneOf(pathWithoutExtension, CoverImageFileContains);
        });
    }

    bool Chm::TryGetCoverFromFirstHtml(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        return TryGetCoverFromHtml(chmFile, HtmlExtensions, outBitmap, outAlphaType);
    }

    bool Chm::TryGetCoverByHtmlPath(chm_file& chmFile, const string& htmlFilePath, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        int fileIndex{ 0 };
        vector<char> xmlFileContent{};
        if (!TryGetFileEndsWithContent(chmFile, fileIndex, xmlFileContent, htmlFilePath))
            return false;

        const Xml::Document xml{ string {xmlFileContent.begin(), xmlFileContent.end()} };
        xmlFileContent.clear();

        return TryGetCoverFromImageTag(chmFile, xml, outBitmap, outAlphaType);
    }

    bool Chm::TryGetCoverFromImageTag(chm_file& chmFile, const Xml::Document& xml, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        string tag, srcValue;
        vector<char> coverFileContent{};
        for (size_t offset = 0, tagOffset = 0; xml.GetTag("img", offset, tagOffset, tag); offset += tag.size())
        {
            offset = tagOffset;
            if (!Xml::Document::GetTagAttribute(tag, "src", srcValue))
                continue;
            PreparePath(srcValue);
            if (!StrLib::EndsWith(srcValue, ImageFileExtensions))
                continue;
            if (const auto pathWithoutExtension = Utility::TrimPathExtension(srcValue); StrLib::EndsWith(pathWithoutExtension, CoverImageFileNotEnds))
                continue;
            if (StrLib::EndsWith(srcValue, ImageFileExtensions) &&
                TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& path) -> bool { return StrLib::EndsWith(path, srcValue); }))
                return true;
        }

        return false;
    }

    void Chm::PreparePath(string& path) const
    {
        StrLib::Trim(path);
        StrLib::UnEscapeXml(path);
        StrLib::TrimStartCi(path, '.');
        StrLib::ReplaceAll(path, '\\', '/');
        StrLib::TrimStartCi(path, string{ "/.." });
        StrLib::ReplaceAll<char>(path, "%20", " ");
        if (const auto anchorOffset = path.find_last_of('#'); anchorOffset != string::npos)
            path.erase(anchorOffset);
    }

    bool Chm::TryGetCoverBitmap(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType, const function<bool(const string&)>& pathMatch)
    {
        vector<char> coverImage{};
        for (int fileIndex{ 0 }; TryGetFileMatchContent(chmFile, fileIndex, coverImage, pathMatch); fileIndex++)
        {
            if (TryLoadBitmap(coverImage, outBitmap, outAlphaType))
                return true;
        }

        return false;
    }

    bool Chm::TryGetCoverFromHtml(chm_file& chmFile, const vector<string>& endsWithStrings, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        vector<char> htmlFileContent{};
        const auto filesCount{ chmFile.n_entries };
        for (const auto& endsWith : endsWithStrings)
        {
            for (int fileIndex{ 0 }; fileIndex < filesCount; ++fileIndex)
            {
                if (!TryGetFileEndsWithContent(chmFile, fileIndex, htmlFileContent, endsWith))
                    continue;

                const Xml::Document html{ string {htmlFileContent.begin(), htmlFileContent.end()} };
                htmlFileContent.clear();

                if (TryGetCoverFromImageTag(chmFile, html, outBitmap, outAlphaType))
                    return true;
            }
        }

        return false;
    }

    bool Chm::TryGetFileMatchContent(chm_file& chmFile, int& fileIndex, vector<char>& outFileContent, const function<bool(const string&)>& pathMatch)
    {
        chm_entry* entry = nullptr;
        size_t depthCount{ SIZE_MAX };
        for (int entryIndex = fileIndex; entryIndex < chmFile.n_entries; entryIndex++)
        {
            const auto currentEntry = chmFile.entries[entryIndex];
            if (!pathMatch(string{ currentEntry->path }))
                continue;

            const size_t currentDepthCount = ranges::count_if(string{ currentEntry->path }, [](const char ch) {return ch == '/'; });
            if (entry != nullptr && currentDepthCount >= depthCount)
                continue;

            fileIndex = entryIndex;
            depthCount = currentDepthCount;
            entry = currentEntry;
        }

        if (entry != nullptr)
            return TryReadFile(chmFile, *entry, outFileContent);

        fileIndex = chmFile.n_entries;
        return false;
    }

    bool Chm::TryGetFileEndsWithContent(chm_file& chmFile, int& fileIndex, vector<char>& outFileContent, const string& endsWith)
    {
        chm_entry* entry = nullptr;
        size_t depthCount{ SIZE_MAX };
        for (int entryIndex = fileIndex; entryIndex < chmFile.n_entries; entryIndex++)
        {
            const auto currentEntry = chmFile.entries[entryIndex];
            const string path{ currentEntry->path };
            if (!StrLib::EndsWith(path, endsWith))
                continue;

            const size_t currentDepthCount = ranges::count_if(path, [](const char ch) {return ch == '/'; });
            if (entry != nullptr && currentDepthCount >= depthCount)
                continue;

            fileIndex = entryIndex;
            depthCount = currentDepthCount;
            entry = currentEntry;
        }

        if (entry != nullptr)
            return TryReadFile(chmFile, *entry, outFileContent);

        fileIndex = chmFile.n_entries;
        return false;
    }

    bool Chm::TryLoadBitmap(const vector<char>& coverImage, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        SIZE imageSize{};
        if (FAILED(Gfx::LoadImageToHBitmap(coverImage.data(), coverImage.size(), outBitmap, outAlphaType, imageSize)))
            return false;

        if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
            return true;

        DeleteObject(outBitmap);
        return false;
    }

    bool Chm::TryReadFile(chm_file& chmFile, chm_entry& fileEntry, vector<char>& outFileContent)
    {
        const auto fileLength = fileEntry.length;

        try { outFileContent.resize(fileLength); }
        catch (...) { return false; }

        const auto readBytes = chm_retrieve_entry(&chmFile, &fileEntry, reinterpret_cast<uint8_t*>(outFileContent.data()), 0, fileLength);
        return readBytes == fileLength;
    }

    int64_t Chm::IStreamReader(void* ctxPtr, void* buffer, const int64_t offset, const int64_t size)
    {
        const auto ctx = static_cast<IStreamReaderCtx*>(ctxPtr);
        if (ctx == nullptr)
            return -1;
        const auto stream = ctx->Stream;
        if (stream == nullptr)
            return -1;

        // Obtain the initial position
        constexpr LARGE_INTEGER fileBeginPosition{ { 0, 0 } };
        ULARGE_INTEGER initialPosition{};
        if (FAILED(stream->Seek(fileBeginPosition, STREAM_SEEK_CUR, &initialPosition)))
            return -1;

        // Set the position to the read offset
        LARGE_INTEGER position;
        position.QuadPart = offset;
        if (FAILED(stream->Seek(position, STREAM_SEEK_SET, nullptr)))
            return -1;

        // Read the data from the position
        ULONG bytesRead{};
        if (const auto result = stream->Read(buffer, static_cast<ULONG>(size), &bytesRead); FAILED(result))
            return result;

        // Restore the original position
        position.QuadPart = static_cast<LONGLONG>(initialPosition.QuadPart);
        if (FAILED(stream->Seek(position, STREAM_SEEK_SET, nullptr)))
            return -1;

        return bytesRead;
    }
}
