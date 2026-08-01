#include "pch.h"
#include "ParserChm.h"
#include "Utility.h"
#include "Gfx.h"
#include "XmlDocument.h"

#include <array>
#include <span>
#include <string_view>

namespace
{
	constexpr size_t MaximumChmArchiveEntries{ 10000U };
	constexpr std::uint64_t MaximumChmEntrySize{ 64ULL * 1024ULL * 1024ULL };
	constexpr ULONGLONG MaximumChmFileSize{ 512ULL * 1024ULL * 1024ULL };

	constexpr std::array<std::string_view, 10> ImageFileExtensions
	{
		".bmp", ".ico", ".gif", ".jpg", ".jpe", ".jfif", ".jpeg", ".png", ".tif", ".tiff"
	};

	constexpr std::array<std::string_view, 3> HtmlExtensions
	{
		".html", ".htm", ".shtml"
	};

	constexpr std::array<std::string_view, 2> TocFileNames
	{
		"toc.html", "toc.htm"
	};

	constexpr std::array<std::string_view, 8> OtherTocFileNames
	{
		"cover.html", "cover.htm", "content.html", "content.htm",
		"index.html", "index.htm", "start.html", "start.htm"
	};

	constexpr std::array<std::string_view, 4> CoverImageFileEnds
	{
		"_xs", "_cover", "/cover", "/cover_01"
	};

	constexpr std::array<std::string_view, 5> CoverImageFileNotEnds
	{
		"next", "previous", "top", "bottom", "_logo"
	};

	constexpr std::array<std::string_view, 1> CoverImageFileContains
	{
		"cover"
	};

	template<typename Character>
	[[nodiscard]] bool EqualsAsciiCaseInsensitive(
		const std::basic_string_view<Character> left,
		const std::basic_string_view<Character> right) noexcept
	{
		if (left.size() != right.size())
			return false;
		for (size_t index = 0; index < left.size(); ++index)
		{
			Character leftCharacter = left[index];
			Character rightCharacter = right[index];
			if (leftCharacter >= static_cast<Character>('A') && leftCharacter <= static_cast<Character>('Z'))
				leftCharacter += static_cast<Character>('a' - 'A');
			if (rightCharacter >= static_cast<Character>('A') && rightCharacter <= static_cast<Character>('Z'))
				rightCharacter += static_cast<Character>('a' - 'A');
			if (leftCharacter != rightCharacter)
				return false;
		}
		return true;
	}

	[[nodiscard]] bool EndsWithAsciiCaseInsensitive(
		const std::string_view value, const std::string_view suffix) noexcept
	{
		return value.size() >= suffix.size() && EqualsAsciiCaseInsensitive(
			value.substr(value.size() - suffix.size()), suffix);
	}

	template<size_t Size>
	[[nodiscard]] bool EndsWithOneOfAsciiCaseInsensitive(
		const std::string_view value,
		const std::array<std::string_view, Size>& suffixes) noexcept
	{
		return std::ranges::any_of(suffixes, [value](const std::string_view suffix)
		{
			return EndsWithAsciiCaseInsensitive(value, suffix);
		});
	}

	[[nodiscard]] bool ContainsAsciiCaseInsensitive(
		const std::string_view value, const std::string_view contained) noexcept
	{
		if (contained.empty())
			return true;
		if (value.size() < contained.size())
			return false;
		for (size_t offset = 0; offset <= value.size() - contained.size(); ++offset)
			if (EqualsAsciiCaseInsensitive(value.substr(offset, contained.size()), contained))
				return true;
		return false;
	}

	template<size_t Size>
	[[nodiscard]] bool ContainsOneOfAsciiCaseInsensitive(
		const std::string_view value,
		const std::array<std::string_view, Size>& candidates) noexcept
	{
		return std::ranges::any_of(candidates, [value](const std::string_view candidate)
		{
			return ContainsAsciiCaseInsensitive(value, candidate);
		});
	}

	struct StreamReleaser final
	{
		void operator()(IStream* const stream) const noexcept
		{
			if (stream == nullptr)
				return;
			try
			{
				stream->Release();
			}
			catch (...)
			{
				// COM methods must not throw across their ABI. A non-conforming stream
				// must still not let cleanup escape this shell extension.
			}
		}
	};

	class ParsedChm final
	{
		chm_file* File;

	public:
		explicit ParsedChm(chm_file& file) noexcept : File{ &file } { }
		ParsedChm(const ParsedChm&) = delete;
		ParsedChm& operator=(const ParsedChm&) = delete;

		~ParsedChm() noexcept
		{
			if (File != nullptr)
				chm_close(File);
		}
	};
}

namespace Parser
{
	bool Chm::CanParse(const wstring& fileExtension)
	{
		return EqualsAsciiCaseInsensitive(
			std::wstring_view{ fileExtension }, std::wstring_view{ L".chm" });
	}

	Result Chm::Parse(const wstring& fileName)
	{
		IStream* rawStream{ nullptr };
		if (const auto result = SHCreateStreamOnFileW(
			fileName.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, &rawStream); FAILED(result))
			return Result{ result };
		const std::unique_ptr<IStream, StreamReleaser> stream{ rawStream };
		return Parse(stream.get());
	}

	Result Chm::Parse(IStream* stream)
	{
        HBITMAP bitmap{ nullptr };
        WTS_ALPHATYPE alphaType{};
		if (!TryGetCoverBitmap(stream, bitmap, alphaType))
			return Result{ E_FAIL };
		return Result{ std::wstring{}, bitmap, alphaType };
	}

	bool Chm::TryGetCoverBitmap(IStream* stream, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
	{
		outBitmap = nullptr;
		outAlphaType = WTSAT_UNKNOWN;
		ULONGLONG streamSize{};
		if (FAILED(Utility::GetIStreamFileSize(stream, streamSize)) ||
			streamSize > MaximumChmFileSize)
			return false;

		chm_file chmFile{};
		IStreamReaderCtx ctx{ stream, streamSize };
		if (!chm_parse(&chmFile, IStreamReader, &ctx))
			return false;
		const ParsedChm parsedChm{ chmFile };
		if (chmFile.entries == nullptr || chmFile.n_entries <= 0 ||
			static_cast<size_t>(chmFile.n_entries) > MaximumChmArchiveEntries)
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
			if (!EndsWithOneOfAsciiCaseInsensitive(path, ImageFileExtensions))
                return false;
            const auto pathWithoutExtension = Utility::TrimPathExtension(path);
			return !EndsWithOneOfAsciiCaseInsensitive(pathWithoutExtension, CoverImageFileNotEnds) &&
				EndsWithOneOfAsciiCaseInsensitive(pathWithoutExtension, CoverImageFileEnds);
        });
    }

    bool Chm::TryGetCoverFromToc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        return TryGetCoverFromHtml(chmFile, TocFileNames, outBitmap, outAlphaType) ||
            TryGetCoverFromHtml(chmFile, OtherTocFileNames, outBitmap, outAlphaType);
    }

    bool Chm::TryGetCoverFromHhc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        vector<char> hhcFileContent{};
		for (int fileIndex{ 0 }; TryGetFileEndsWithContent(
			chmFile, fileIndex, hhcFileContent, ".hhc"); ++fileIndex)
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
				EqualsAsciiCaseInsensitive(
					std::string_view{ attributeValue }, std::string_view{ "Name" });
        };

        auto isParamTagIndicatesCoverObject = [](const string& paramTag) -> bool
        {
            string attributeValue{};
            return Xml::Document::GetTagAttribute(paramTag, "name", attributeValue) &&
				EqualsAsciiCaseInsensitive(
					std::string_view{ attributeValue }, std::string_view{ "Name" }) &&
                Xml::Document::GetTagAttribute(paramTag, "value", attributeValue) &&
				EqualsAsciiCaseInsensitive(
					std::string_view{ attributeValue }, std::string_view{ "Cover" });
        };

        auto isParamTagWithCoverPath = [](const string& paramTag) -> bool
        {
            string attributeValue{};
            return Xml::Document::GetTagAttribute(paramTag, "name", attributeValue) &&
				EqualsAsciiCaseInsensitive(
					std::string_view{ attributeValue }, std::string_view{ "Local" });
        };

        string elementContent;
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

		if (EndsWithOneOfAsciiCaseInsensitive(path, HtmlExtensions))
            return TryGetCoverByHtmlPath(chmFile, path, outBitmap, outAlphaType);

		if (EndsWithOneOfAsciiCaseInsensitive(path, ImageFileExtensions))
			return TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& filePath) -> bool
			{
				return EndsWithAsciiCaseInsensitive(filePath, path);
			});

        return false;
    }

    bool Chm::TryGetCoverByFileName(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
    {
        return TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& path) -> bool
        {
			if (!EndsWithOneOfAsciiCaseInsensitive(path, ImageFileExtensions))
                return false;
            const auto pathWithoutExtension = Utility::TrimPathExtension(path);
			return !EndsWithOneOfAsciiCaseInsensitive(pathWithoutExtension, CoverImageFileNotEnds) &&
				ContainsOneOfAsciiCaseInsensitive(pathWithoutExtension, CoverImageFileContains);
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
		if (!TryGetFileEndsWithContent(
			chmFile, fileIndex, xmlFileContent, std::string_view{ htmlFilePath }))
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
			if (!EndsWithOneOfAsciiCaseInsensitive(srcValue, ImageFileExtensions))
                continue;
			if (const auto pathWithoutExtension = Utility::TrimPathExtension(srcValue);
				EndsWithOneOfAsciiCaseInsensitive(pathWithoutExtension, CoverImageFileNotEnds))
                continue;
			if (TryGetCoverBitmap(chmFile, outBitmap, outAlphaType, [&](const string& path) -> bool
			{
				return EndsWithAsciiCaseInsensitive(path, srcValue);
			}))
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

	bool Chm::TryGetCoverFromHtml(
		chm_file& chmFile, const std::span<const std::string_view> endsWithStrings,
		HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
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
		if (chmFile.entries == nullptr || fileIndex < 0)
			return false;

        chm_entry* entry = nullptr;
        size_t depthCount{ SIZE_MAX };
        for (int entryIndex = fileIndex; entryIndex < chmFile.n_entries; entryIndex++)
        {
            const auto currentEntry = chmFile.entries[entryIndex];
			if (currentEntry == nullptr)
				continue;
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

	bool Chm::TryGetFileEndsWithContent(
		chm_file& chmFile, int& fileIndex, vector<char>& outFileContent,
		const std::string_view endsWith)
    {
		if (chmFile.entries == nullptr || fileIndex < 0)
			return false;

        chm_entry* entry = nullptr;
        size_t depthCount{ SIZE_MAX };
        for (int entryIndex = fileIndex; entryIndex < chmFile.n_entries; entryIndex++)
        {
            const auto currentEntry = chmFile.entries[entryIndex];
			if (currentEntry == nullptr)
				continue;
            const string path{ currentEntry->path };
			if (!EndsWithAsciiCaseInsensitive(path, endsWith))
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
		outBitmap = nullptr;
		outAlphaType = WTSAT_UNKNOWN;
        return false;
    }

    bool Chm::TryReadFile(chm_file& chmFile, chm_entry& fileEntry, vector<char>& outFileContent)
    {
		outFileContent.clear();
		const auto fileLength = fileEntry.length;
		if (fileLength < 0 || static_cast<std::uint64_t>(fileLength) > MaximumChmEntrySize ||
			static_cast<std::uint64_t>(fileLength) > outFileContent.max_size())
			return false;

		try
		{
			outFileContent.resize(static_cast<size_t>(fileLength));
		}
		catch (...)
		{
			return false;
		}

		const auto readBytes = chm_retrieve_entry(
			&chmFile, &fileEntry, reinterpret_cast<uint8_t*>(outFileContent.data()), 0, fileLength);
		if (readBytes != fileLength)
		{
			outFileContent.clear();
			return false;
		}
		return true;
    }

	int64_t Chm::IStreamReader(
		void* ctxPtr, void* buffer, const int64_t offset, const int64_t size) noexcept
	{
		IStream* stream{};
		ULARGE_INTEGER initialPosition{};
		bool capturedPosition{};

		auto restorePosition = [&]() noexcept -> bool
		{
			if (!capturedPosition || stream == nullptr)
				return true;
			if (initialPosition.QuadPart >
				static_cast<ULONGLONG>((std::numeric_limits<LONGLONG>::max)()))
				return false;
			try
			{
				LARGE_INTEGER position{};
				position.QuadPart = static_cast<LONGLONG>(initialPosition.QuadPart);
				return SUCCEEDED(stream->Seek(position, STREAM_SEEK_SET, nullptr));
			}
			catch (...)
			{
				return false;
			}
		};

		try
		{
			const auto ctx = static_cast<IStreamReaderCtx*>(ctxPtr);
			if (ctx == nullptr || ctx->Stream == nullptr || offset < 0 || size < 0 ||
				size > static_cast<int64_t>((std::numeric_limits<ULONG>::max)()) ||
				(buffer == nullptr && size != 0))
				return -1;
			const auto unsignedOffset = static_cast<ULONGLONG>(offset);
			const auto unsignedSize = static_cast<ULONGLONG>(size);
			if (unsignedOffset > ctx->Size || unsignedSize > ctx->Size - unsignedOffset)
				return -1;
			stream = ctx->Stream;

			constexpr LARGE_INTEGER currentPosition{};
			if (FAILED(stream->Seek(currentPosition, STREAM_SEEK_CUR, &initialPosition)))
				return -1;
			capturedPosition = true;

			LARGE_INTEGER readPosition{};
			readPosition.QuadPart = offset;
			if (FAILED(stream->Seek(readPosition, STREAM_SEEK_SET, nullptr)))
			{
				static_cast<void>(restorePosition());
				return -1;
			}

			ULONG bytesRead{};
			const HRESULT readResult = stream->Read(
				buffer, static_cast<ULONG>(size), &bytesRead);
			const bool restored = restorePosition();
			return SUCCEEDED(readResult) && restored &&
				bytesRead <= static_cast<ULONG>(size) ? bytesRead : -1;
		}
		catch (...)
		{
			static_cast<void>(restorePosition());
			return -1;
		}
	}
}
