#include <Windows.h>
#include <Shlwapi.h>
#include <objbase.h>
#include <thumbcache.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Use an explicit relative path here. On Windows, minizip's zip.h and
// LiveIcons' Zip.h otherwise differ only by case.
#include "../zlib/contrib/minizip/zip.h"
#include "../zlib/contrib/minizip/unzip.h"
#include "../zlib/contrib/minizip/iowin32.h"

#include "../LiveIcons/ParserBase.h"
#include "../LiveIcons/ParserChm.h"
#include "../LiveIcons/ParserEpub.h"
#include "../LiveIcons/ParserFb2.h"
#include "../LiveIcons/ParserMobi.h"
#include "../LiveIcons/StrLib.h"
#include "../LiveIcons/ZipArchive.h"
#include "../LiveIcons/ZipCache.h"

#if defined(LIVEICONS_TEST_ENABLE_CBR)
#include "../LiveIcons/ParserCbr.h"
#endif

namespace
{
    namespace fs = std::filesystem;

    constexpr LONG FixtureWidth = 32;
    constexpr LONG FixtureHeight = 48;

    [[noreturn]] void Fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void Require(const bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    std::string Narrow(const std::wstring& value)
    {
        if (value.empty())
            return {};

        const auto required = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            nullptr, 0, nullptr, nullptr);
        if (required <= 0)
            return "<unprintable path>";

        std::string converted(static_cast<size_t>(required), '\0');
        if (WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                converted.data(), required, nullptr, nullptr) <= 0)
            return "<unprintable path>";
        return converted;
    }

    std::string HResultText(const HRESULT value)
    {
        std::ostringstream text;
        text << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
             << static_cast<std::uint32_t>(value);
        return text.str();
    }

    class Suite final
    {
        size_t Passed{};
        size_t Failed{};
        size_t Skipped{};

    public:
        template<typename Function>
        bool Run(const std::string& name, Function&& function)
        {
            try
            {
                std::invoke(std::forward<Function>(function));
                ++Passed;
                std::cout << "[PASS] " << name << '\n';
                return true;
            }
            catch (const std::exception& error)
            {
                ++Failed;
                std::cout << "[FAIL] " << name << ": " << error.what() << '\n';
                return false;
            }
            catch (...)
            {
                ++Failed;
                std::cout << "[FAIL] " << name << ": unknown exception\n";
                return false;
            }
        }

        void Skip(const std::string& name, const std::string& reason)
        {
            ++Skipped;
            std::cout << "[SKIP] " << name << ": " << reason << '\n';
        }

        [[nodiscard]] int ExitCode() const
        {
            return Failed == 0 ? 0 : 1;
        }

        void PrintSummary() const
        {
            std::cout << "\nSummary: " << Passed << " passed, " << Failed << " failed, "
                      << Skipped << " skipped.\n";
        }
    };

    class ComApartment final
    {
        HRESULT Result;
        bool MustUninitialize;

    public:
        ComApartment() :
            Result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
            MustUninitialize(SUCCEEDED(Result))
        {
        }

        ComApartment(const ComApartment&) = delete;
        ComApartment& operator=(const ComApartment&) = delete;

        ~ComApartment()
        {
            if (MustUninitialize)
                CoUninitialize();
        }

        [[nodiscard]] HRESULT GetResult() const { return Result; }
    };

    class StreamOwner final
    {
        IStream* Stream{};

    public:
        explicit StreamOwner(IStream* stream = nullptr) : Stream(stream) { }
        StreamOwner(const StreamOwner&) = delete;
        StreamOwner& operator=(const StreamOwner&) = delete;

        StreamOwner(StreamOwner&& other) noexcept : Stream(std::exchange(other.Stream, nullptr)) { }
        StreamOwner& operator=(StreamOwner&& other) noexcept
        {
            if (this != &other)
            {
                if (Stream != nullptr)
                    Stream->Release();
                Stream = std::exchange(other.Stream, nullptr);
            }
            return *this;
        }

        ~StreamOwner()
        {
            if (Stream != nullptr)
                Stream->Release();
        }

        [[nodiscard]] IStream* Get() const { return Stream; }
    };

    class BitmapOwner final
    {
        HBITMAP Bitmap{};

    public:
        explicit BitmapOwner(const HBITMAP bitmap) : Bitmap(bitmap) { }
        BitmapOwner(const BitmapOwner&) = delete;
        BitmapOwner& operator=(const BitmapOwner&) = delete;

        ~BitmapOwner()
        {
            if (Bitmap != nullptr)
                DeleteObject(Bitmap);
        }

        [[nodiscard]] HBITMAP Get() const { return Bitmap; }
    };

    class TempDirectory final
    {
        fs::path Path;

    public:
        TempDirectory()
        {
            std::error_code error;
            const auto root = fs::temp_directory_path(error);
            Require(!error, "Unable to resolve the temporary directory: " + error.message());

            const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
            const auto tick = static_cast<unsigned long long>(GetTickCount64());
            for (unsigned attempt = 0; attempt < 100; ++attempt)
            {
                auto candidate = root / (L"LiveIconsTests-" + std::to_wstring(process) + L"-" +
                                         std::to_wstring(tick) + L"-" + std::to_wstring(attempt));
                error.clear();
                if (fs::create_directory(candidate, error))
                {
                    Path = std::move(candidate);
                    return;
                }
                if (error)
                    Fail("Unable to create the temporary fixture directory: " + error.message());
            }
            Fail("Unable to choose a unique temporary fixture directory");
        }

        TempDirectory(const TempDirectory&) = delete;
        TempDirectory& operator=(const TempDirectory&) = delete;

        ~TempDirectory()
        {
            if (Path.empty())
                return;
            std::error_code ignored;
            fs::remove_all(Path, ignored);
        }

        [[nodiscard]] const fs::path& Get() const { return Path; }
    };

    class ZipWriter final
    {
        zipFile File{};

    public:
        explicit ZipWriter(const fs::path& path)
        {
            zlib_filefunc64_def functions{};
            fill_win32_filefunc64W(&functions);
            File = zipOpen2_64(path.c_str(), APPEND_STATUS_CREATE, nullptr, &functions);
            Require(File != nullptr, "Unable to create EPUB fixture");
        }

        ZipWriter(const ZipWriter&) = delete;
        ZipWriter& operator=(const ZipWriter&) = delete;

        ~ZipWriter()
        {
            if (File != nullptr)
                static_cast<void>(zipClose(File, nullptr));
        }

        void Add(const std::string& name, const std::span<const char> content, const bool compress)
        {
            Require(content.size() <= std::numeric_limits<unsigned>::max(), "Fixture ZIP entry is too large");
            zip_fileinfo info{};
            const auto openResult = zipOpenNewFileInZip64(
                File, name.c_str(), &info, nullptr, 0, nullptr, 0, nullptr,
                compress ? Z_DEFLATED : 0,
                compress ? Z_DEFAULT_COMPRESSION : 0,
                0);
            Require(openResult == ZIP_OK, "Unable to add ZIP entry " + name +
                                              " (error " + std::to_string(openResult) + ")");

            const auto writeResult = zipWriteInFileInZip(
                File, content.data(), static_cast<unsigned>(content.size()));
            const auto closeResult = zipCloseFileInZip(File);
            Require(writeResult == ZIP_OK, "Unable to write ZIP entry " + name +
                                               " (error " + std::to_string(writeResult) + ")");
            Require(closeResult == ZIP_OK, "Unable to close ZIP entry " + name +
                                               " (error " + std::to_string(closeResult) + ")");
        }

        void Close()
        {
            Require(File != nullptr, "EPUB fixture ZIP is already closed");
            const auto result = zipClose(File, nullptr);
            File = nullptr;
            Require(result == ZIP_OK, "Unable to finalize EPUB fixture (error " +
                                          std::to_string(result) + ")");
        }
    };

    class UnzipOwner final
    {
        unzFile File{};

    public:
        explicit UnzipOwner(const fs::path& path)
        {
            zlib_filefunc64_def functions{};
            fill_win32_filefunc64W(&functions);
            File = unzOpen2_64(path.c_str(), &functions);
            Require(File != nullptr, "Unable to open fixture with minizip");
        }

        UnzipOwner(const UnzipOwner&) = delete;
        UnzipOwner& operator=(const UnzipOwner&) = delete;

        ~UnzipOwner()
        {
            if (File != nullptr)
                static_cast<void>(unzClose(File));
        }

        [[nodiscard]] unzFile Get() const { return File; }
    };

    std::vector<char> MakeBmp(const LONG width, const LONG height)
    {
        Require(width > 0 && height > 0, "Invalid BMP dimensions");
        const auto rowSize = static_cast<size_t>((width * 3 + 3) & ~3);
        const auto pixelSize = rowSize * static_cast<size_t>(height);

        BITMAPFILEHEADER fileHeader{};
        fileHeader.bfType = 0x4D42;
        fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        fileHeader.bfSize = static_cast<DWORD>(fileHeader.bfOffBits + pixelSize);

        BITMAPINFOHEADER infoHeader{};
        infoHeader.biSize = sizeof(infoHeader);
        infoHeader.biWidth = width;
        infoHeader.biHeight = height;
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 24;
        infoHeader.biCompression = BI_RGB;
        infoHeader.biSizeImage = static_cast<DWORD>(pixelSize);

        std::vector<char> bytes(fileHeader.bfSize, 0);
        std::memcpy(bytes.data(), &fileHeader, sizeof(fileHeader));
        std::memcpy(bytes.data() + sizeof(fileHeader), &infoHeader, sizeof(infoHeader));

        auto* pixels = reinterpret_cast<unsigned char*>(bytes.data() + fileHeader.bfOffBits);
        for (LONG y = 0; y < height; ++y)
        {
            for (LONG x = 0; x < width; ++x)
            {
                const auto offset = static_cast<size_t>(y) * rowSize + static_cast<size_t>(x) * 3;
                pixels[offset] = static_cast<unsigned char>(48 + (x * 5) % 160);      // blue
                pixels[offset + 1] = static_cast<unsigned char>(32 + (y * 3) % 192);  // green
                pixels[offset + 2] = static_cast<unsigned char>(224 - (x + y) % 96);  // red
            }
        }
        return bytes;
    }

    std::string Base64Encode(const std::span<const char> bytes)
    {
        static constexpr char Alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string encoded;
        encoded.reserve(((bytes.size() + 2) / 3) * 4);
        for (size_t offset = 0; offset < bytes.size(); offset += 3)
        {
            const auto remaining = bytes.size() - offset;
            const auto first = static_cast<unsigned char>(bytes[offset]);
            const auto second = remaining > 1 ? static_cast<unsigned char>(bytes[offset + 1]) : 0;
            const auto third = remaining > 2 ? static_cast<unsigned char>(bytes[offset + 2]) : 0;
            const auto packed = (static_cast<std::uint32_t>(first) << 16) |
                                (static_cast<std::uint32_t>(second) << 8) |
                                static_cast<std::uint32_t>(third);

            encoded.push_back(Alphabet[(packed >> 18) & 0x3F]);
            encoded.push_back(Alphabet[(packed >> 12) & 0x3F]);
            encoded.push_back(remaining > 1 ? Alphabet[(packed >> 6) & 0x3F] : '=');
            encoded.push_back(remaining > 2 ? Alphabet[packed & 0x3F] : '=');
        }
        return encoded;
    }

    void WriteFile(const fs::path& path, const std::span<const char> content)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        Require(output.is_open(), "Unable to create " + Narrow(path.wstring()));
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        Require(output.good(), "Unable to write " + Narrow(path.wstring()));
    }

    std::vector<char> ReadFile(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        Require(input.is_open(), "Unable to open " + Narrow(path.wstring()));
        const auto size = input.tellg();
        Require(size >= 0, "Unable to determine file size for " + Narrow(path.wstring()));
        input.seekg(0);

        std::vector<char> bytes(static_cast<size_t>(size));
        if (!bytes.empty())
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        Require(input.good() || input.eof(), "Unable to read " + Narrow(path.wstring()));
        return bytes;
    }

    StreamOwner MakeMemoryStream(const std::span<const char> bytes)
    {
        Require(bytes.size() <= std::numeric_limits<UINT>::max(), "Fixture is too large for SHCreateMemStream");
        auto* stream = SHCreateMemStream(
            reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size()));
        Require(stream != nullptr, "SHCreateMemStream failed");
        return StreamOwner{ stream };
    }

    StreamOwner OpenFileStream(const fs::path& path)
    {
        IStream* stream{};
        const auto result = SHCreateStreamOnFileEx(
            path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL,
            FALSE, nullptr, &stream);
        Require(SUCCEEDED(result), "SHCreateStreamOnFileEx failed for " + Narrow(path.wstring()) +
                                       " (" + HResultText(result) + ")");
        return StreamOwner{ stream };
    }

    void VerifyParseResult(
        Parser::Result& result,
        const std::optional<std::wstring>& expectedTitle = std::nullopt,
        const std::optional<SIZE>& expectedSize = std::nullopt)
    {
        BitmapOwner bitmap(result.Cover);
        result.Cover = nullptr;

        std::string details;
        if (!result.Error.empty())
            details = ": " + Narrow(result.Error);
        Require(SUCCEEDED(result.HResult), "Parser returned " + HResultText(result.HResult) + details);
        Require(bitmap.Get() != nullptr, "Parser reported success without an HBITMAP");

        BITMAP bitmapInfo{};
        Require(GetObjectW(bitmap.Get(), sizeof(bitmapInfo), &bitmapInfo) == sizeof(bitmapInfo),
                "GetObjectW failed for returned HBITMAP");
        Require(bitmapInfo.bmWidth > 0 && bitmapInfo.bmHeight != 0,
                "Returned HBITMAP has invalid dimensions");

        if (expectedSize.has_value())
        {
            Require(bitmapInfo.bmWidth == expectedSize->cx &&
                        std::abs(bitmapInfo.bmHeight) == expectedSize->cy,
                    "Unexpected cover dimensions: got " + std::to_string(bitmapInfo.bmWidth) + "x" +
                        std::to_string(std::abs(bitmapInfo.bmHeight)) + ", expected " +
                        std::to_string(expectedSize->cx) + "x" + std::to_string(expectedSize->cy));
            Require(result.CoverAlpha == WTSAT_ARGB,
                    "Deterministic WIC fixture did not report WTSAT_ARGB");
        }

        if (expectedTitle.has_value())
            Require(result.Title == *expectedTitle,
                    "Unexpected title: got '" + Narrow(result.Title) + "', expected '" +
                        Narrow(*expectedTitle) + "'");
    }

    struct Fixtures final
    {
        fs::path EpubPath;
        std::vector<char> EpubBytes;
        fs::path Fb2Path;
        std::vector<char> Fb2Bytes;
    };

    Fixtures CreateFixtures(const fs::path& directory)
    {
        const auto cover = MakeBmp(FixtureWidth, FixtureHeight);

        Fixtures fixtures{};
        fixtures.Fb2Path = directory / L"generated-cover.fb2";
        const auto fb2 = std::string{
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<FictionBook xmlns:l=\"http://www.w3.org/1999/xlink\">"
            "<description><title-info><coverpage><image l:href=\"#cover-image\"/>"
            "</coverpage></title-info></description>"
            "<binary id=\"cover-image\" content-type=\"image/bmp\">" } +
            Base64Encode(cover) + "</binary></FictionBook>";
        fixtures.Fb2Bytes.assign(fb2.begin(), fb2.end());
        WriteFile(fixtures.Fb2Path, fixtures.Fb2Bytes);

        fixtures.EpubPath = directory / L"generated-cover.epub";
        const std::string mimetype{ "application/epub+zip" };
        const std::string container{
            "<?xml version=\"1.0\"?>"
            "<container><rootfiles><rootfile full-path=\"OPS/content.opf\"/>"
            "</rootfiles></container>" };
        const std::string package{
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<package><metadata><title>LiveIcons Regression Fixture</title>"
            "<meta name=\"cover\" content=\"cover-image\"/></metadata>"
            "<manifest><item id=\"cover-image\" href=\"images/cover.bmp\" "
            "media-type=\"image/bmp\"/></manifest></package>" };

        ZipWriter writer(fixtures.EpubPath);
        writer.Add("mimetype", mimetype, false);
        writer.Add("META-INF/container.xml", container, true);
        writer.Add("OPS/content.opf", package, true);
        writer.Add("OPS/images/cover.bmp", cover, true);
        writer.Close();
        fixtures.EpubBytes = ReadFile(fixtures.EpubPath);
        return fixtures;
    }

    struct ParserRoute final
    {
        std::string Name;
        std::unique_ptr<Parser::Base> Parser;
    };

    std::vector<ParserRoute> MakeParserRoutes()
    {
        std::vector<ParserRoute> routes;
        routes.push_back({ "EPUB", std::make_unique<Parser::Epub>() });
        routes.push_back({ "FB2", std::make_unique<Parser::Fb2>() });
        routes.push_back({ "MOBI", std::make_unique<Parser::Mobi>() });
        routes.push_back({ "CHM", std::make_unique<Parser::Chm>() });
#if defined(LIVEICONS_TEST_ENABLE_CBR)
        routes.push_back({ "CBR", std::make_unique<Parser::Cbr>() });
#endif
        return routes;
    }

    ParserRoute* FindRoute(std::vector<ParserRoute>& routes, const std::wstring& extension)
    {
        for (auto& route : routes)
        {
            if (route.Parser->CanParse(extension))
                return &route;
        }
        return nullptr;
    }

    void TestRouting()
    {
        auto routes = MakeParserRoutes();
        const std::vector<std::wstring> stableExtensions
        {
            L".epub", L".fb2", L".mobi", L".azw", L".azw3", L".chm"
        };

        for (const auto& extension : stableExtensions)
        {
            const auto count = std::ranges::count_if(routes, [&](const ParserRoute& route)
            {
                return route.Parser->CanParse(extension);
            });
            Require(count == 1, "Extension " + Narrow(extension) + " routes to " +
                                    std::to_string(count) + " parsers");

            auto uppercase = extension;
            std::ranges::transform(uppercase, uppercase.begin(),
                [](const wchar_t character) { return static_cast<wchar_t>(towupper(character)); });
            const auto uppercaseCount = std::ranges::count_if(routes, [&](const ParserRoute& route)
            {
                return route.Parser->CanParse(uppercase);
            });
            Require(uppercaseCount == 1, "Uppercase extension " + Narrow(uppercase) +
                                             " did not route case-insensitively");
        }

        for (const auto& extension : { L"", L".zip", L".txt" })
            Require(FindRoute(routes, extension) == nullptr,
                    "Unsupported extension unexpectedly routed: " + Narrow(extension));

#if defined(LIVEICONS_TEST_ENABLE_CBR)
        Require(FindRoute(routes, L".cbr") != nullptr, "CBR tests are enabled but .cbr is not routed");
#else
        Require(FindRoute(routes, L".cbr") == nullptr, "Unfinished .cbr route is unexpectedly active");
#endif
    }

    void TestArchiveCache(const fs::path& epubPath)
    {
        Zip::Archive archive(epubPath.wstring());
        Require(archive.Open() == UNZ_OK, "Zip::Archive::Open failed");
        Require(archive.FileExists("ops/IMAGES/COVER.BMP"),
                "Zip::Archive::FileExists lost case-insensitive matching");

        std::vector<char> content;
        Require(archive.ReadPath("meta-inf/CONTAINER.XML", content),
                "Zip::Archive::ReadPath failed after a prior full-cache search");
        const std::string container(content.begin(), content.end());
        Require(container.find("OPS/content.opf") != std::string::npos,
                "Zip::Archive returned unexpected container.xml content");

        const auto* position = archive.Find(
            [](const std::string& path) { return StrLib::EndsWith(path, std::string{ ".opf" }); }, 2);
        Require(position != END_OF_LIST && position->FileIndex == 2,
                "Zip::Archive::Find(startIndex) did not restore the cached OPF position");
        content.clear();
        Require(archive.ReadCurrent(content) && !content.empty(),
                "Zip::Archive::ReadCurrent failed at a cached position");
    }

    void TestRawCacheTraversal(const fs::path& epubPath)
    {
        UnzipOwner unzip(epubPath);
        Zip::Cache cache(unzip.Get());

        const auto* position = cache.First();
        Require(position != END_OF_LIST && position->FileIndex == 0 && position->FilePath == "mimetype",
                "Cache::First returned the wrong entry");
        Require(cache.Current() != END_OF_LIST && cache.Current()->FileIndex == 0,
                "Cache::Current did not track Cache::First");

        position = cache.Next();
        Require(position != END_OF_LIST && position->FileIndex == 1 &&
                    position->FilePath == "META-INF/container.xml",
                "Cache::Next returned the wrong entry");

        position = cache.At(0);
        Require(position != END_OF_LIST && position->FilePath == "mimetype",
                "Cache::At could not rewind to a cached entry");
        position = cache.Next();
        Require(position != END_OF_LIST && position->FileIndex == 1,
                "Cache::Next could not advance through an already cached entry");

        position = cache.At(3);
        Require(position != END_OF_LIST && position->FileIndex == 3 &&
                    position->FilePath == "OPS/images/cover.bmp",
                "Cache::At could not populate up to a requested entry");
        Require(cache.At(99) == END_OF_LIST, "Cache::At returned an entry past end-of-list");

        position = cache.At(2);
        Require(position != END_OF_LIST && position->FilePath == "OPS/content.opf",
                "Cache::At could not revisit an entry after end-of-list");
        cache.SetCurrent(*position);
        Require(cache.Current() != END_OF_LIST && cache.Current()->FileIndex == 2,
                "Cache::SetCurrent did not synchronize Current()");
    }

    void RunSelfTests(Suite& suite)
    {
        TempDirectory temporary;
        Fixtures fixtures{};
        if (!suite.Run("fixture generation", [&]
            {
                fixtures = CreateFixtures(temporary.Get());
            }))
            return;

        suite.Run("parser extension routing", TestRouting);
        suite.Run("ZIP archive/cache integration", [&] { TestArchiveCache(fixtures.EpubPath); });
        suite.Run("ZIP cache traversal and rewind", [&] { TestRawCacheTraversal(fixtures.EpubPath); });

        suite.Run("EPUB path parsing", [&]
        {
            Parser::Epub parser;
            auto result = parser.Parse(fixtures.EpubPath.wstring());
            VerifyParseResult(result, L"LiveIcons Regression Fixture", SIZE{ FixtureWidth, FixtureHeight });
        });

        suite.Run("EPUB IStream parsing", [&]
        {
            Parser::Epub parser;
            auto stream = MakeMemoryStream(fixtures.EpubBytes);
            auto result = parser.Parse(stream.Get());
            VerifyParseResult(result, L"LiveIcons Regression Fixture", SIZE{ FixtureWidth, FixtureHeight });
        });

        suite.Run("FB2 path parsing", [&]
        {
            Parser::Fb2 parser;
            auto result = parser.Parse(fixtures.Fb2Path.wstring());
            VerifyParseResult(result, std::nullopt, SIZE{ FixtureWidth, FixtureHeight });
        });

        suite.Run("FB2 IStream parsing", [&]
        {
            Parser::Fb2 parser;
            auto stream = MakeMemoryStream(fixtures.Fb2Bytes);
            auto result = parser.Parse(stream.Get());
            VerifyParseResult(result, std::nullopt, SIZE{ FixtureWidth, FixtureHeight });
        });
    }

    bool IsCbr(const fs::path& path)
    {
        return StrLib::EqualsCi(path.extension().wstring(), std::wstring{ L".cbr" });
    }

    void RunCorpus(Suite& suite, const fs::path& corpusRoot)
    {
        auto routes = MakeParserRoutes();
        std::vector<fs::path> files;
        size_t recognizedFiles{};

        const auto discovered = suite.Run("corpus discovery", [&]
        {
            std::error_code error;
            Require(fs::exists(corpusRoot, error) && !error,
                    "Corpus path does not exist: " + Narrow(corpusRoot.wstring()));
            Require(fs::is_directory(corpusRoot, error) && !error,
                    "Corpus path is not a directory: " + Narrow(corpusRoot.wstring()));

            fs::recursive_directory_iterator iterator(
                corpusRoot, fs::directory_options::skip_permission_denied, error);
            const fs::recursive_directory_iterator end;
            Require(!error, "Unable to enumerate corpus: " + error.message());

            for (; iterator != end; iterator.increment(error))
            {
                if (error)
                {
                    error.clear();
                    continue;
                }
                if (!iterator->is_regular_file(error) || error)
                {
                    error.clear();
                    continue;
                }

                const auto extension = iterator->path().extension().wstring();
                if (FindRoute(routes, extension) != nullptr || IsCbr(iterator->path()))
                {
                    files.push_back(iterator->path());
                    ++recognizedFiles;
                }
            }

            std::ranges::sort(files, [](const fs::path& left, const fs::path& right)
            {
                return left.native() < right.native();
            });
            Require(recognizedFiles != 0,
                    "No EPUB, FB2, MOBI/AZW/AZW3, CHM, or CBR files were found");
        });
        if (!discovered)
            return;

        for (const auto& path : files)
        {
            auto* route = FindRoute(routes, path.extension().wstring());
            const auto displayPath = Narrow(path.wstring());
            if (route == nullptr)
            {
                suite.Skip("corpus/CBR/" + displayPath,
                           "build with /p:LiveIconsEnableCbrTests=true after ParserCbr is ready");
                continue;
            }

            suite.Run("corpus/path/" + route->Name + "/" + displayPath, [&]
            {
                auto result = route->Parser->Parse(path.wstring());
                VerifyParseResult(result);
            });

            suite.Run("corpus/IStream/" + route->Name + "/" + displayPath, [&]
            {
                auto stream = OpenFileStream(path);
                auto result = route->Parser->Parse(stream.Get());
                VerifyParseResult(result);
            });
        }
    }

    void PrintUsage()
    {
        std::cout
            << "LiveIconsTests [--self-test]\n"
            << "LiveIconsTests --corpus <directory>\n"
            << "LiveIconsTests --all <directory>\n\n"
            << "No arguments runs deterministic generated-fixture tests. --corpus parses\n"
            << "supported files recursively by path and IStream. --all runs both.\n";
    }
}

int wmain(const int argumentCount, wchar_t* arguments[])
{
    SetConsoleOutputCP(CP_UTF8);

    bool runSelfTests{};
    bool runCorpus{};
    fs::path corpusPath;

    if (argumentCount == 1 ||
        (argumentCount == 2 && std::wstring_view{ arguments[1] } == L"--self-test"))
    {
        runSelfTests = true;
    }
    else if (argumentCount == 3 && std::wstring_view{ arguments[1] } == L"--corpus")
    {
        runCorpus = true;
        corpusPath = arguments[2];
    }
    else if (argumentCount == 3 && std::wstring_view{ arguments[1] } == L"--all")
    {
        runSelfTests = true;
        runCorpus = true;
        corpusPath = arguments[2];
    }
    else if (argumentCount == 2 &&
             (std::wstring_view{ arguments[1] } == L"--help" ||
              std::wstring_view{ arguments[1] } == L"-h"))
    {
        PrintUsage();
        return 0;
    }
    else
    {
        PrintUsage();
        return 2;
    }

    try
    {
        const ComApartment com;
        if (FAILED(com.GetResult()))
        {
            std::cerr << "COM initialization failed: " << HResultText(com.GetResult()) << '\n';
            return 2;
        }

        Suite suite;
        if (runSelfTests)
            RunSelfTests(suite);
        if (runCorpus)
            RunCorpus(suite, corpusPath);
        suite.PrintSummary();
        return suite.ExitCode();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Fatal test harness error: " << error.what() << '\n';
        return 2;
    }
    catch (...)
    {
        std::cerr << "Fatal test harness error: unknown exception\n";
        return 2;
    }
}
