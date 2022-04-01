#include "pch.h"
#include "ParserChm.h"
#include "Utility.h"
#include "Gfx.h"
#include "XmlDocument.h"
#include "DataIStream.h"

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
        ".htm"
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
        "index.html",
        "index.htm",
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

        // Find an image file with the name that ends with _xs, those files are usually the cover images
        if (TryGetCoverFromXsFile(chmFile, outBitmap, outAlphaType))
            return true;

        // Locate toc.html/toc.htm file get the first image that looks like a cover
        if (TryGetCoverFromToc(chmFile, outBitmap, outAlphaType))
            return true;

        // Read HHC objects and check if there is an object with a cover, load referenced covet file
        if (TryGetCoverFromHhc(chmFile, outBitmap, outAlphaType))
            return true;

        return false;
    }

    bool Chm::TryGetCoverFromXsFile(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        return TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& path) -> bool
        {
            if (!StrLib::EndsWith(path, ImageFileExtensions))
                return false;
            const auto extensionOffset = path.find_last_of('.');
            const auto pathWithoutExtension = extensionOffset != string::npos ? path.substr(0, extensionOffset) : path;
            return StrLib::EndsWith(pathWithoutExtension, std::string{ "_xs" });
        });
    }

    bool Chm::TryGetCoverFromToc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        int fileIndex{ 0 };
        vector<char> tocFileContent{};

        if (!TryGetFileContent(chmFile, fileIndex, tocFileContent, [&](const string& path) -> bool { return StrLib::EndsWith(path, TocFileNames); }) &&
            !TryGetFileContent(chmFile, fileIndex, tocFileContent, [&](const string& path) -> bool { return StrLib::EndsWith(path, OtherTocFileNames); }))
            return false;

        const Xml::Document tocXml{ string {tocFileContent} };
        tocFileContent.clear();

        return TryGetCoverFromImageTag(chmFile, tocXml, outBitmap, outAlphaType);
    }

    bool Chm::TryGetCoverFromHhc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        const string hhcExtension{ ".hhc" };

        auto isParamTagIndicatesNameObject = [](const string& paramTag) -> bool
        {
            string attributeValue{};
            return Xml::Document::GetTagAttribute(paramTag, "name", attributeValue) &&
                StrLib::EqualsCi(attributeValue, string{ "Name" });
        };

        auto isParamTagWithCoverPath = [](const string& paramTag) -> bool
        {
            string attributeValue{};
            return Xml::Document::GetTagAttribute(paramTag, "name", attributeValue) &&
                StrLib::EqualsCi(attributeValue, string{ "Local" });
        };

        vector<char> hhcFileContent{};
        for (int fileIndex{ 0 }; TryGetFileContent(chmFile, fileIndex, hhcFileContent, [&](const string& path) -> bool { return StrLib::EndsWith(path, hhcExtension); }); ++fileIndex)
        {
            const Xml::Document hhcXml{ string {hhcFileContent} };
            hhcFileContent.clear();
            if (TryGetPathFromHhcObjects(chmFile, hhcXml, isParamTagIndicatesNameObject, isParamTagWithCoverPath, outBitmap, outAlphaType))
                return true;
        }

        return false;
    }

    bool Chm::TryGetPathFromHhcObjects(chm_file& chmFile, const Xml::Document& hhcXml,
        const function<bool(const string&)>& isParamTagIndicatesCoverObject,
        const function<bool(const string&)>& isParamTagWithCoverPath,
        HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        string elementContent;
        const string objectElementName{ "object" };
        for (size_t elementSearchOffset = 0, contentOffset = 0; hhcXml.GetElementContent("object", elementSearchOffset, elementContent, &contentOffset); elementSearchOffset += elementContent.size())
        {
            elementSearchOffset = contentOffset;
            const Xml::Document hhcObjectXml{ string {elementContent} }; //CAUTION! Do not inline! Possible bug in VS2022.
            if (TryGetPathFromHhcObject(chmFile, hhcObjectXml, isParamTagIndicatesCoverObject, isParamTagWithCoverPath, outBitmap, outAlphaType))
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

    bool Chm::TryGetCoverByHtmlPath(chm_file& chmFile, const string& htmlFilePath, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        int fileIndex{ 0 };
        vector<char> xmlFileContent{};
        if (!TryGetFileContent(chmFile, fileIndex, xmlFileContent, [&](const string& htmlPath) -> bool { return StrLib::EndsWith(htmlPath, htmlFilePath); }))
            return false;

        const Xml::Document xml{ string {xmlFileContent} };
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
            if (StrLib::EndsWith(srcValue, ImageFileExtensions) &&
                TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& path) -> bool { return StrLib::EndsWith(path, srcValue); }))
                return true;
        }

        return false;
    }

    void Chm::PreparePath(string& path) const
    {
        StrLib::UnEscapeXml(path);
        StrLib::TrimStartCi(path, '.');
        StrLib::ReplaceAll(path, '\\', '/');
        StrLib::TrimStartCi(path, string{ "/.." });

        const auto anchorOffset = path.find_last_of('#');
        if (anchorOffset == string::npos)
            return;
        path.resize(anchorOffset);
    }

    bool Chm::TryGetCoverBitmap(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType, const function<bool(const string&)>& pathMatch)
    {
        vector<char> coverImage{};
        for (int fileIndex{ 0 }; TryGetFileContent(chmFile, fileIndex, coverImage, pathMatch); fileIndex++)
        {
            if (TryLoadBitmap(coverImage, outBitmap, outAlphaType))
                return true;
        }

        return false;
    }

    bool Chm::TryGetFileContent(chm_file& chmFile, int& fileIndex, vector<char>& outFileContent, const function<bool(const string&)>& pathMatch)
    {
        for (int entryIndex = fileIndex; entryIndex < chmFile.n_entries; entryIndex++)
        {
            if (const auto entry = chmFile.entries[entryIndex];
                !pathMatch(string{ entry->path }) ||
                !TryReadFile(chmFile, *entry, outFileContent))
                continue;
            fileIndex = entryIndex;
            return true;
        }

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
