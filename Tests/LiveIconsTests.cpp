#include <Windows.h>
#include <Shlwapi.h>
#include <objbase.h>
#include <thumbcache.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
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
#include <thread>
#include <type_traits>
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
#include "../LiveIcons/RamFile.h"
#include "../LiveIcons/Log.h"
#include "../LiveIcons/StrLib.h"
#include "../LiveIcons/Utility.h"
#include "../LiveIcons/ZipArchive.h"
#include "../LiveIcons/ZipCache.h"
#include "../chmlib/src/lzx.h"

extern "C"
{
#include "../libmobi/src/compression.h"

    MOBI_RET mobi_parse_cdic(
        MOBIHuffCdic* huffcdic,
        const MOBIPdbRecord* record,
        size_t recordNumber);
}

static_assert(!std::is_copy_constructible_v<Parser::Result>);
static_assert(!std::is_copy_assignable_v<Parser::Result>);
static_assert(std::is_nothrow_move_constructible_v<Parser::Result>);
static_assert(std::is_nothrow_move_assignable_v<Parser::Result>);
static_assert(noexcept(Log::NextCorrelationId()));
static_assert(noexcept(Log::Diagnostic(Log::EventId::Logger, S_OK)));
static_assert(noexcept(Log::Error(Log::EventId::Logger, E_FAIL)));
static_assert(noexcept(Log::Exception(Log::EventId::Logger, E_UNEXPECTED)));

#if defined(LIVEICONS_TEST_ENABLE_CBR)
#include "../LiveIcons/ParserCbr.h"
#endif

namespace
{
    namespace fs = std::filesystem;

    constexpr LONG FixtureWidth = 32;
    constexpr LONG FixtureHeight = 48;
    constexpr LONG CbrFixtureWidth = 40;
    constexpr LONG CbrFixtureHeight = 60;
    constexpr size_t CbrFixtureArchiveSize = 7444;
    constexpr size_t SolidCbrFixtureArchiveSize = 1332;

    [[noreturn]] void Fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void Require(const bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

#if defined(LIVEICONS_TEST_ENABLE_CBR)
    void RequireFixtureBytes(
        const std::vector<char>& bytes,
        const size_t offset,
        const std::span<const unsigned char> expected,
        const std::string_view description)
    {
        Require(offset <= bytes.size() && expected.size() <= bytes.size() - offset,
                std::string{ description } + " fixture range is out of bounds");
        for (size_t index = 0; index < expected.size(); ++index)
            Require(static_cast<unsigned char>(bytes[offset + index]) == expected[index],
                    std::string{ description } + " fixture bytes have changed");
    }

    void ReplaceFixtureBytes(
        std::vector<char>& bytes,
        const size_t offset,
        const std::span<const unsigned char> replacement)
    {
        Require(offset <= bytes.size() && replacement.size() <= bytes.size() - offset,
                "Replacement fixture range is out of bounds");
        for (size_t index = 0; index < replacement.size(); ++index)
            bytes[offset + index] = static_cast<char>(replacement[index]);
    }
#endif

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

    class Win32HandleOwner final
    {
        HANDLE Handle{};

    public:
        explicit Win32HandleOwner(const HANDLE handle = nullptr) noexcept : Handle(handle) { }
        Win32HandleOwner(const Win32HandleOwner&) = delete;
        Win32HandleOwner& operator=(const Win32HandleOwner&) = delete;

        ~Win32HandleOwner()
        {
            if (Handle != nullptr && Handle != INVALID_HANDLE_VALUE)
                CloseHandle(Handle);
        }

        [[nodiscard]] HANDLE Get() const noexcept { return Handle; }
    };

    enum class InjectedException
    {
        None,
        BadAllocation,
        Standard,
        Unknown
    };

    void ThrowInjectedException(const InjectedException exception)
    {
        switch (exception)
        {
        case InjectedException::None:
            return;
        case InjectedException::BadAllocation:
            throw std::bad_alloc{};
        case InjectedException::Standard:
            throw std::runtime_error{ "injected IStream failure" };
        case InjectedException::Unknown:
            throw 7;
        }
    }

    class ConfigurableStream final : public IStream
    {
        LONG References{ 1 };

    public:
        explicit ConfigurableStream(std::vector<char> bytes = {}) :
            Bytes(std::move(bytes)), ReportedSize(Bytes.size())
        {
        }

        std::vector<char> Bytes;
        ULONGLONG ReportedSize{};
        ULONGLONG Position{};
        ULONG MaximumReadPerCall{ (std::numeric_limits<ULONG>::max)() };

        InjectedException StatException{ InjectedException::None };
        InjectedException SeekException{ InjectedException::None };
        InjectedException ReadException{ InjectedException::None };
        size_t SeekExceptionOnCall{ 1 };
        size_t ReadExceptionOnCall{ 1 };
        size_t ZeroReadOnCall{};
        size_t OverReportedReadOnCall{};
        size_t FailedReadOnCall{};
        HRESULT ReadFailure{ STG_E_READFAULT };

        bool ProvideStatName{};
        std::wstring StatName;
        size_t StatNameAllocations{};
        DWORD LastStatFlag{ (std::numeric_limits<DWORD>::max)() };
        size_t StatCalls{};
        size_t SeekCalls{};
        size_t ReadCalls{};

        HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID interfaceId, void** const outObject) override
        {
            if (outObject == nullptr)
                return E_POINTER;
            *outObject = nullptr;
            if (!IsEqualIID(interfaceId, IID_IUnknown) &&
                !IsEqualIID(interfaceId, IID_ISequentialStream) &&
                !IsEqualIID(interfaceId, IID_IStream))
                return E_NOINTERFACE;
            *outObject = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&References));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const LONG references = InterlockedDecrement(&References);
            if (references == 0)
                delete this;
            return static_cast<ULONG>(references);
        }

        HRESULT STDMETHODCALLTYPE Read(
            void* const buffer, const ULONG requested, ULONG* const outRead) override
        {
            ++ReadCalls;
            if (ReadException != InjectedException::None &&
                ReadCalls == ReadExceptionOnCall)
                ThrowInjectedException(ReadException);

            if (outRead != nullptr)
                *outRead = 0;
            if (buffer == nullptr && requested != 0)
                return STG_E_INVALIDPOINTER;
            if (ReadCalls == FailedReadOnCall)
                return ReadFailure;
            if (ReadCalls == ZeroReadOnCall)
                return S_OK;
            if (ReadCalls == OverReportedReadOnCall)
            {
                if (outRead != nullptr)
                    *outRead = requested + 1;
                return S_OK;
            }

            const size_t available = Position < Bytes.size()
                ? Bytes.size() - static_cast<size_t>(Position)
                : 0;
            const size_t readSize = (std::min)({
                available,
                static_cast<size_t>(requested),
                static_cast<size_t>(MaximumReadPerCall) });
            if (readSize != 0)
            {
                if (buffer == nullptr)
                    return STG_E_INVALIDPOINTER;
                std::memcpy(buffer, Bytes.data() + static_cast<size_t>(Position), readSize);
                Position += readSize;
            }
            if (outRead != nullptr)
                *outRead = static_cast<ULONG>(readSize);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Write(
            const void*, const ULONG, ULONG* const outWritten) override
        {
            if (outWritten != nullptr)
                *outWritten = 0;
            return STG_E_ACCESSDENIED;
        }

        HRESULT STDMETHODCALLTYPE Seek(
            const LARGE_INTEGER move, const DWORD origin,
            ULARGE_INTEGER* const outNewPosition) override
        {
            ++SeekCalls;
            if (SeekException != InjectedException::None &&
                SeekCalls == SeekExceptionOnCall)
                ThrowInjectedException(SeekException);

            ULONGLONG base{};
            switch (origin)
            {
            case STREAM_SEEK_SET:
                break;
            case STREAM_SEEK_CUR:
                base = Position;
                break;
            case STREAM_SEEK_END:
                base = ReportedSize;
                break;
            default:
                return STG_E_INVALIDFUNCTION;
            }

            ULONGLONG newPosition{};
            if (move.QuadPart >= 0)
            {
                const auto distance = static_cast<ULONGLONG>(move.QuadPart);
                if (base > (std::numeric_limits<ULONGLONG>::max)() - distance)
                    return STG_E_INVALIDFUNCTION;
                newPosition = base + distance;
            }
            else
            {
                const auto distance = static_cast<ULONGLONG>(-(move.QuadPart + 1)) + 1;
                if (distance > base)
                    return STG_E_INVALIDFUNCTION;
                newPosition = base - distance;
            }

            Position = newPosition;
            if (outNewPosition != nullptr)
                outNewPosition->QuadPart = Position;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override
        {
            return STG_E_ACCESSDENIED;
        }

        HRESULT STDMETHODCALLTYPE CopyTo(
            IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override
        {
            return STG_E_INVALIDFUNCTION;
        }
        HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override
        {
            return STG_E_INVALIDFUNCTION;
        }

        HRESULT STDMETHODCALLTYPE Stat(STATSTG* const stat, const DWORD flags) override
        {
            ++StatCalls;
            LastStatFlag = flags;
            ThrowInjectedException(StatException);
            if (stat == nullptr)
                return E_POINTER;

            *stat = {};
            stat->type = STGTY_STREAM;
            stat->cbSize.QuadPart = ReportedSize;
            if (flags != STATFLAG_NONAME && ProvideStatName)
            {
                const size_t byteCount = (StatName.size() + 1) * sizeof(wchar_t);
                auto* const name = static_cast<wchar_t*>(CoTaskMemAlloc(byteCount));
                if (name == nullptr)
                    return E_OUTOFMEMORY;
                std::memcpy(name, StatName.c_str(), byteCount);
                stat->pwcsName = name;
                ++StatNameAllocations;
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Clone(IStream** const outStream) override
        {
            if (outStream != nullptr)
                *outStream = nullptr;
            return E_NOTIMPL;
        }
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

    template<typename Value>
    Value ReadFixtureValue(const std::vector<char>& bytes, const size_t offset)
    {
        if (offset > bytes.size() || sizeof(Value) > bytes.size() - offset)
            Fail("CHM fixture metadata is outside the file");
        Value value{};
        std::memcpy(&value, bytes.data() + offset, sizeof value);
        return value;
    }

    void WriteFixtureInt32(
        std::vector<char>& bytes, const size_t offset, const std::int32_t value)
    {
        if (offset > bytes.size() || sizeof value > bytes.size() - offset)
            Fail("CHM fixture mutation is outside the file");
        std::memcpy(bytes.data() + offset, &value, sizeof value);
    }

    fs::path GetExecutableDirectory()
    {
        std::vector<wchar_t> modulePath(32768);
        const auto length = GetModuleFileNameW(
            nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        Require(length != 0 && length < modulePath.size(),
                "Unable to resolve the test executable directory");
        return fs::path{ std::wstring{ modulePath.data(), length } }.parent_path();
    }

    std::vector<char> DecodeBase64Fixture(const fs::path& path)
    {
        const auto encodedBytes = ReadFile(path);
        Require(encodedBytes.size() <= std::numeric_limits<DWORD>::max(),
                "Base64 fixture is too large");

        DWORD decodedSize{};
        const auto encodedSize = static_cast<DWORD>(encodedBytes.size());
        Require(CryptStringToBinaryA(
                    encodedBytes.data(), encodedSize, CRYPT_STRING_BASE64,
                    nullptr, &decodedSize, nullptr, nullptr) != FALSE,
                "Unable to determine decoded size for " + Narrow(path.wstring()));

        std::vector<char> decoded(decodedSize);
        Require(CryptStringToBinaryA(
                    encodedBytes.data(), encodedSize, CRYPT_STRING_BASE64,
                    reinterpret_cast<BYTE*>(decoded.data()), &decodedSize, nullptr, nullptr) != FALSE,
                "Unable to decode " + Narrow(path.wstring()));
        decoded.resize(decodedSize);
        return decoded;
    }

    std::vector<char> LoadRar5Fixture(
        const std::wstring_view fixtureName, const size_t expectedArchiveSize)
    {
        const auto encodedPath = GetExecutableDirectory() / L"Fixtures" / fixtureName;
        auto bytes = DecodeBase64Fixture(encodedPath);
        Require(bytes.size() == expectedArchiveSize,
                "Decoded CBR fixture has an unexpected size: " + Narrow(std::wstring{ fixtureName }));

        static constexpr unsigned char Rar5Signature[]{ 'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00 };
        Require(bytes.size() >= sizeof(Rar5Signature) &&
                    std::memcmp(bytes.data(), Rar5Signature, sizeof(Rar5Signature)) == 0,
                "Decoded CBR fixture does not have a RAR5 signature: " +
                    Narrow(std::wstring{ fixtureName }));
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
        BitmapOwner bitmap(result.ReleaseCover());

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

    void VerifyParseFailure(Parser::Result& result)
    {
        BitmapOwner bitmap(result.ReleaseCover());

        Require(FAILED(result.HResult), "Parser unexpectedly reported success");
        Require(bitmap.Get() == nullptr, "Parser returned an HBITMAP with a failure result");
        Require(!result.Error.empty(), "Parser failure did not include a diagnostic message");
    }

    struct Fixtures final
    {
        fs::path EpubPath;
        std::vector<char> EpubBytes;
        fs::path Fb2Path;
        std::vector<char> Fb2Bytes;
#if defined(LIVEICONS_TEST_ENABLE_CBR)
        fs::path CbrPath;
        std::vector<char> CbrBytes;
        fs::path SolidCbrPath;
        std::vector<char> SolidCbrBytes;
        fs::path OversizedDictionaryCbrPath;
        std::vector<char> OversizedDictionaryCbrBytes;
        fs::path MultiVolumeCbrPath;
        std::vector<char> MultiVolumeCbrBytes;
        fs::path InvalidCbrPath;
        std::vector<char> InvalidCbrBytes;
#endif
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

#if defined(LIVEICONS_TEST_ENABLE_CBR)
        fixtures.CbrBytes = LoadRar5Fixture(
            L"cbr-rar5-first-image.rar.base64", CbrFixtureArchiveSize);
        fixtures.CbrPath = directory / L"generated-cover.cbr";
        WriteFile(fixtures.CbrPath, fixtures.CbrBytes);

        fixtures.SolidCbrBytes = LoadRar5Fixture(
            L"cbr-rar5-solid-first-image.rar.base64", SolidCbrFixtureArchiveSize);
        fixtures.SolidCbrPath = directory / L"generated-solid-cover.cbr";
        WriteFile(fixtures.SolidCbrPath, fixtures.SolidCbrBytes);

        fixtures.OversizedDictionaryCbrBytes = fixtures.CbrBytes;
        constexpr std::array<unsigned char, 2> dictionaryField{ 0x80, 0x00 };
        constexpr std::array<unsigned char, 1> oversizedDictionaryExponent{ 0x59 };
        constexpr std::array<unsigned char, 4> oversizedDictionaryHeaderCrc{
            0x9E, 0x38, 0x4F, 0xD2 };
        RequireFixtureBytes(
            fixtures.OversizedDictionaryCbrBytes, 0xA6, dictionaryField,
            "RAR5 dictionary");
        ReplaceFixtureBytes(
            fixtures.OversizedDictionaryCbrBytes, 0xA7,
            oversizedDictionaryExponent);
        ReplaceFixtureBytes(
            fixtures.OversizedDictionaryCbrBytes, 0x95,
            oversizedDictionaryHeaderCrc);
        fixtures.OversizedDictionaryCbrPath = directory / L"oversized-dictionary.cbr";
        WriteFile(
            fixtures.OversizedDictionaryCbrPath,
            fixtures.OversizedDictionaryCbrBytes);

        fixtures.MultiVolumeCbrBytes = fixtures.CbrBytes;
        constexpr std::array<unsigned char, 1> singleVolumeFlag{ 0x00 };
        constexpr std::array<unsigned char, 1> multiVolumeFlag{ 0x01 };
        constexpr std::array<unsigned char, 4> multiVolumeHeaderCrc{
            0x57, 0xEF, 0x58, 0x21 };
        RequireFixtureBytes(
            fixtures.MultiVolumeCbrBytes, 0x0F, singleVolumeFlag,
            "RAR5 volume flag");
        ReplaceFixtureBytes(
            fixtures.MultiVolumeCbrBytes, 0x0F, multiVolumeFlag);
        ReplaceFixtureBytes(
            fixtures.MultiVolumeCbrBytes, 0x08, multiVolumeHeaderCrc);
        fixtures.MultiVolumeCbrPath = directory / L"multi-volume.cbr";
        WriteFile(fixtures.MultiVolumeCbrPath, fixtures.MultiVolumeCbrBytes);

        fixtures.InvalidCbrBytes = fixtures.CbrBytes;
        fixtures.InvalidCbrBytes.front() = 'X';
        fixtures.InvalidCbrPath = directory / L"invalid-signature.cbr";
        WriteFile(fixtures.InvalidCbrPath, fixtures.InvalidCbrBytes);
#endif
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

    void TestResultBitmapOwnership()
    {
        const HBITMAP bitmap = CreateBitmap(2, 2, 1, 32, nullptr);
		if (bitmap == nullptr)
			Fail("CreateBitmap failed for Result ownership test");

        {
            Parser::Result original{ std::wstring{}, bitmap, WTSAT_ARGB };
            Parser::Result moved{ std::move(original) };
            Require(original.GetCover() == nullptr,
                    "Moving Parser::Result left two owners of the HBITMAP");
            Require(moved.GetCover() == bitmap,
                    "Moving Parser::Result did not transfer its HBITMAP");
        }

        BITMAP bitmapInfo{};
        Require(GetObjectW(bitmap, sizeof(bitmapInfo), &bitmapInfo) == 0,
                "Destroying Parser::Result did not release its HBITMAP");
    }

    void TestRouting()
    {
        auto routes = MakeParserRoutes();
        std::vector<std::wstring> stableExtensions
        {
            L".epub", L".fb2", L".mobi", L".azw", L".azw3", L".chm"
        };
#if defined(LIVEICONS_TEST_ENABLE_CBR)
        stableExtensions.push_back(L".cbr");
#endif

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
        Require(FindRoute(routes, L".cbr") == nullptr, "Disabled .cbr route is unexpectedly active");
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
		if (position == END_OF_LIST)
			Fail("Cache::First did not return an entry");
		Require(position->FileIndex == 0 && position->FilePath == "mimetype",
                "Cache::First returned the wrong entry");
        const auto* const firstPosition = position;
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
        Require(firstPosition->FileIndex == 0 && firstPosition->FilePath == "mimetype",
                "Growing the ZIP cache invalidated a previously returned position");

        position = cache.At(2);
		if (position == END_OF_LIST)
			Fail("Cache::At could not revisit an entry after end-of-list");
		Require(position->FilePath == "OPS/content.opf",
                "Cache::At could not revisit an entry after end-of-list");
        cache.SetCurrent(*position);
        Require(cache.Current() != END_OF_LIST && cache.Current()->FileIndex == 2,
                "Cache::SetCurrent did not synchronize Current()");
    }

    void TestRamFileSnapshot()
    {
        const std::vector<char> expected{ 'L', 'I', 'V', 'E' };
        auto stream = MakeMemoryStream(expected);
        Utility::RamFile snapshot{ stream.Get() };
        Require(SUCCEEDED(snapshot.GetHResult()),
                "RamFile failed to create a temporary stream snapshot");
        Require(snapshot.GetFileHandle() != nullptr,
                "RamFile reported success after losing ownership of its FILE handle");

        std::array<char, 4> actual{};
        Require(fread(actual.data(), 1, actual.size(), snapshot.GetFileHandle()) == actual.size(),
                "RamFile snapshot could not be read through its CRT FILE handle");
        Require(std::equal(actual.begin(), actual.end(), expected.begin()),
                "RamFile snapshot content differs from its source stream");
    }

    void TestToAbsolutePath()
    {
        Require(
            Utility::ToAbsolutePath(
                "OPS/text/chapter.xhtml", "../images/./cover.jpg") ==
                "OPS/images/cover.jpg",
            "ToAbsolutePath did not normalize a relative archive path");
        Require(
            Utility::ToAbsolutePath("OPS/content.opf", "../../escape.jpg").empty(),
            "ToAbsolutePath allowed traversal above the archive root");
    }

    void TestReadIStreamExactReadsAndRestore()
    {
        std::vector<char> expected(150 * 1024 + 17);
        for (size_t index = 0; index < expected.size(); ++index)
            expected[index] = static_cast<char>((index * 37U + 11U) & 0xFFU);

        auto* const rawStream = new ConfigurableStream{ expected };
        StreamOwner stream{ rawStream };
        rawStream->MaximumReadPerCall = 4096;
        rawStream->Position = 29;

        std::vector<char> actual{ 's', 't', 'a', 'l', 'e' };
        const HRESULT result = Utility::ReadIStream(stream.Get(), actual);
        Require(result == S_OK,
                "ReadIStream rejected a valid multi-chunk stream (" +
                    HResultText(result) + ")");
        Require(actual == expected, "ReadIStream did not return the exact stream bytes");
        Require(rawStream->ReadCalls > 1,
                "The exact-read test did not exercise multiple IStream::Read calls");
        Require(rawStream->Position == 29,
                "ReadIStream did not restore the original position after success");
    }

    void TestReadIStreamFaultsAndRestore()
    {
        {
            auto* const rawStream = new ConfigurableStream{
                std::vector<char>(16, 'x') };
            StreamOwner stream{ rawStream };
            rawStream->ReportedSize = 32;
            rawStream->Position = 7;

            std::vector<char> content{ 's', 't', 'a', 'l', 'e' };
            const HRESULT result = Utility::ReadIStream(stream.Get(), content);
            Require(result == STG_E_READFAULT,
                    "ReadIStream did not reject a short stream (" +
                        HResultText(result) + ")");
            Require(content.empty(), "A short read left partial output visible");
            Require(rawStream->ReadCalls == 2,
                    "The short-read test did not reach the zero-byte read");
            Require(rawStream->Position == 7,
                    "ReadIStream did not restore the original position after a short read");
        }

        {
            auto* const rawStream = new ConfigurableStream{
                std::vector<char>(16, 'y') };
            StreamOwner stream{ rawStream };
            rawStream->Position = 5;
            rawStream->OverReportedReadOnCall = 1;

            std::vector<char> content{ 's', 't', 'a', 'l', 'e' };
            const HRESULT result = Utility::ReadIStream(stream.Get(), content);
            Require(result == STG_E_READFAULT,
                    "ReadIStream accepted an over-reported byte count (" +
                        HResultText(result) + ")");
            Require(content.empty(), "An over-reported read left output visible");
            Require(rawStream->Position == 5,
                    "ReadIStream did not restore the position after an invalid byte count");
        }
    }

    void TestReadIStreamExceptionTranslation()
    {
        struct ExceptionCase final
        {
            InjectedException Exception;
            HRESULT Expected;
            std::string_view Name;
        };
        constexpr std::array cases
        {
            ExceptionCase{ InjectedException::BadAllocation, E_OUTOFMEMORY, "bad_alloc" },
            ExceptionCase{ InjectedException::Standard, E_UNEXPECTED, "std::exception" },
            ExceptionCase{ InjectedException::Unknown, E_UNEXPECTED, "unknown exception" }
        };

        for (const auto& test : cases)
        {
            {
                auto* const rawStream = new ConfigurableStream{
                    std::vector<char>(8, 's') };
                StreamOwner stream{ rawStream };
                rawStream->StatException = test.Exception;

                std::vector<char> content{ 'x' };
                const HRESULT result = Utility::ReadIStream(stream.Get(), content);
                Require(result == test.Expected,
                        "Stat " + std::string{ test.Name } + " mapped to " +
                            HResultText(result));
                Require(content.empty(),
                        "A throwing Stat call left output visible");
                Require(rawStream->SeekCalls == 0 && rawStream->ReadCalls == 0,
                        "ReadIStream continued after Stat threw");
            }

            {
                auto* const rawStream = new ConfigurableStream{
                    std::vector<char>(8, 'k') };
                StreamOwner stream{ rawStream };
                rawStream->Position = 3;
                rawStream->SeekException = test.Exception;
                rawStream->SeekExceptionOnCall = 2;

                std::vector<char> content{ 'x' };
                const HRESULT result = Utility::ReadIStream(stream.Get(), content);
                Require(result == test.Expected,
                        "Seek " + std::string{ test.Name } + " mapped to " +
                            HResultText(result));
                Require(content.empty(),
                        "A throwing Seek call left output visible");
                Require(rawStream->Position == 3,
                        "ReadIStream did not restore the position after Seek threw");
            }

            {
                auto* const rawStream = new ConfigurableStream{
                    std::vector<char>(8, 'r') };
                StreamOwner stream{ rawStream };
                rawStream->Position = 4;
                rawStream->ReadException = test.Exception;

                std::vector<char> content{ 'x' };
                const HRESULT result = Utility::ReadIStream(stream.Get(), content);
                Require(result == test.Expected,
                        "Read " + std::string{ test.Name } + " mapped to " +
                            HResultText(result));
                Require(content.empty(),
                        "A throwing Read call left output visible");
                Require(rawStream->Position == 4,
                        "ReadIStream did not restore the position after Read threw");
            }
        }
    }

    void TestReadIStreamSizeLimit()
    {
        constexpr ULONGLONG maximumInputBytes = 512ULL * 1024ULL * 1024ULL;
        auto* const rawStream = new ConfigurableStream;
        StreamOwner stream{ rawStream };
        rawStream->ReportedSize = maximumInputBytes + 1;

        std::vector<char> content{ 's', 't', 'a', 'l', 'e' };
        const HRESULT result = Utility::ReadIStream(stream.Get(), content);
        Require(result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
                "ReadIStream did not reject a stream above 512 MiB (" +
                    HResultText(result) + ")");
        Require(content.empty(), "An oversized stream left output visible");
        Require(rawStream->SeekCalls == 0 && rawStream->ReadCalls == 0,
                "ReadIStream touched stream data before rejecting its reported size");
    }

    void TestStreamMetadataUtilities()
    {
        {
            auto* const rawStream = new ConfigurableStream;
            StreamOwner stream{ rawStream };
            std::wstring fileName{ L"stale" };
            const HRESULT result = Utility::GetIStreamFileName(stream.Get(), fileName);
            Require(result == STG_E_INVALIDPOINTER,
                    "GetIStreamFileName accepted a null STATSTG name (" +
                        HResultText(result) + ")");
            Require(fileName.empty(),
                    "GetIStreamFileName did not clear output for a null name");
        }

        {
            auto* const rawStream = new ConfigurableStream;
            StreamOwner stream{ rawStream };
            rawStream->ProvideStatName = true;
            rawStream->StatName = L"C:\\Books\\cover.epub";

            std::wstring fileName{ L"stale" };
            const HRESULT result = Utility::GetIStreamFileName(stream.Get(), fileName);
            Require(result == S_OK,
                    "GetIStreamFileName rejected a CoTaskMem name (" +
                        HResultText(result) + ")");
            Require(fileName == rawStream->StatName,
                    "GetIStreamFileName did not copy the CoTaskMem name");
            Require(rawStream->StatNameAllocations == 1 &&
                        rawStream->LastStatFlag == STATFLAG_DEFAULT,
                    "GetIStreamFileName did not request and consume a STATSTG name");
        }

        std::wstring extension{ L"stale" };
        HRESULT result = Utility::GetFileExtension(L"book-without-extension", extension);
        Require(result == HRESULT_FROM_WIN32(ERROR_INVALID_NAME),
                "GetFileExtension accepted a name without an extension");
        Require(extension.empty(),
                "GetFileExtension did not clear output on failure");

        result = Utility::GetFileExtension(L"book.EPUB", extension);
        Require(result == S_OK && extension == L".EPUB",
                "GetFileExtension did not replace output on success");
    }

    void TestTryParseNumberStrictness()
    {
        size_t number{};
        Require(Utility::TryParseNumber("0", number) && number == 0,
                "TryParseNumber rejected zero");
        Require(Utility::TryParseNumber("0012", number) && number == 12,
                "TryParseNumber rejected a complete decimal number");

        const std::string maximum = std::to_string((std::numeric_limits<size_t>::max)());
        Require(Utility::TryParseNumber(maximum, number) &&
                    number == (std::numeric_limits<size_t>::max)(),
                "TryParseNumber rejected size_t's maximum value");

        std::vector<std::string> invalid
        {
            "", " 1", "1 ", "+1", "-1", "12x", "1.0"
        };
        invalid.push_back(maximum + "0");
        for (const auto& value : invalid)
        {
            number = 37;
            Require(!Utility::TryParseNumber(value, number),
                    "TryParseNumber accepted '" + value + "'");
            Require(number == 37,
                    "TryParseNumber modified output after rejecting '" + value + "'");
        }
    }

    template<typename ParserType>
    void VerifyMalformedStreamFailure(
        const std::string_view parserName, std::vector<char> bytes)
    {
        auto* const rawStream = new ConfigurableStream{ std::move(bytes) };
        StreamOwner stream{ rawStream };
        ParserType parser;
        auto result = parser.Parse(stream.Get());
        BitmapOwner bitmap{ result.ReleaseCover() };
        Require(FAILED(result.HResult),
                std::string{ parserName } + " accepted malformed/truncated input");
        Require(bitmap.Get() == nullptr,
                std::string{ parserName } + " returned a cover with a failure result");
    }

    void TestMalformedParserInputs()
    {
        VerifyMalformedStreamFailure<Parser::Epub>(
            "EPUB", { 'P', 'K', 3, 4, 0, 0, 0, 0 });

        const std::string fb2{
            "<FictionBook><binary content-type=\"image/png\">%%%" };
        VerifyMalformedStreamFailure<Parser::Fb2>(
            "FB2", std::vector<char>{ fb2.begin(), fb2.end() });

        VerifyMalformedStreamFailure<Parser::Mobi>(
            "MOBI", { 'B', 'O', 'O', 'K', 'M', 'O', 'B', 'I' });
        VerifyMalformedStreamFailure<Parser::Chm>(
            "CHM", { 'I', 'T', 'S', 'F', 3, 0, 0, 0 });
    }

    class ChmFileOwner final
    {
        chm_file File{};
        mem_reader_ctx Context{};
        bool Parsed{};

    public:
        ChmFileOwner() = default;
        ChmFileOwner(const ChmFileOwner&) = delete;
        ChmFileOwner& operator=(const ChmFileOwner&) = delete;

        ~ChmFileOwner()
        {
            if (Parsed)
                chm_close(&File);
        }

        bool Parse(std::vector<char>& bytes)
        {
            mem_reader_init(&Context, bytes.data(), static_cast<std::int64_t>(bytes.size()));
            Parsed = chm_parse(&File, mem_reader, &Context);
            return Parsed;
        }

        [[nodiscard]] chm_file& Get() noexcept { return File; }
    };

    void TestChmNativeHardening()
    {
        Require(lzx_init(-1) == nullptr && lzx_init(22) == nullptr,
                "LZX accepted an invalid window exponent");

        auto* const oversizedRawStream = new ConfigurableStream{
            std::vector<char>{ 'I', 'T', 'S', 'F' } };
        oversizedRawStream->ReportedSize = 512ULL * 1024ULL * 1024ULL + 1ULL;
        StreamOwner oversizedStream{ oversizedRawStream };
        Parser::Chm parser;
        auto oversizedResult = parser.Parse(oversizedStream.Get());
        Require(FAILED(oversizedResult.HResult) && oversizedRawStream->ReadCalls == 0,
                "CHM parser read an input that exceeded its whole-file budget");

        const auto fixturePath =
            GetExecutableDirectory() / L"Fixtures" / L"DotZLib.chm";
        auto validBytes = ReadFile(fixturePath);
        Require(validBytes.size() <=
                    static_cast<size_t>((std::numeric_limits<std::int64_t>::max)()),
                "CHM fixture is too large");

        {
            ChmFileOwner archive;
            Require(archive.Parse(validBytes), "CHMLib rejected the valid CHM fixture");
            Require(archive.Get().entries != nullptr && archive.Get().n_entries > 0 &&
                        archive.Get().n_entries <= 10000,
                    "CHMLib produced an invalid entry table");

            bool retrievedCompressedData{};
            std::array<unsigned char, 4096> buffer{};
            for (int index = 0; index < archive.Get().n_entries; ++index)
            {
                auto* const entry = archive.Get().entries[index];
                if (entry == nullptr || entry->space != CHM_COMPRESSED || entry->length <= 0)
                    continue;
                const auto requested = (std::min)(
                    entry->length, static_cast<std::int64_t>(buffer.size()));
                retrievedCompressedData = chm_retrieve_entry(
                    &archive.Get(), entry, buffer.data(), 0, requested) == requested;
                if (retrievedCompressedData)
                    break;
            }
            Require(retrievedCompressedData,
                    "CHMLib could not retrieve a compressed entry from the valid fixture");
        }

        const auto directoryOffset = ReadFixtureValue<std::uint64_t>(validBytes, 0x48);
        Require(directoryOffset <= validBytes.size() &&
                    directoryOffset + 0x54 <= validBytes.size(),
                "CHM fixture has an invalid directory offset");
        const auto directory = static_cast<size_t>(directoryOffset);
        const auto blockLength =
            ReadFixtureValue<std::uint32_t>(validBytes, directory + 0x10);
        const auto indexHead =
            ReadFixtureValue<std::int32_t>(validBytes, directory + 0x20);
        Require(indexHead >= 0 && blockLength != 0,
                "CHM fixture has invalid directory-page metadata");
        const auto pageOffset = directoryOffset + 0x54ULL +
            static_cast<std::uint64_t>(indexHead) * blockLength;
        Require(pageOffset <= validBytes.size() && pageOffset + 0x14 <= validBytes.size(),
                "CHM fixture directory page is outside the file");

        auto cyclicBytes = validBytes;
        WriteFixtureInt32(cyclicBytes, static_cast<size_t>(pageOffset) + 0x10, indexHead);
        ChmFileOwner cyclicArchive;
        Require(!cyclicArchive.Parse(cyclicBytes),
                "CHMLib accepted a cyclic PMGL directory chain");

        auto emptyPathBytes = validBytes;
        emptyPathBytes[static_cast<size_t>(pageOffset) + 0x14] = 0;
        ChmFileOwner emptyPathArchive;
        Require(!emptyPathArchive.Parse(emptyPathBytes),
                "CHMLib accepted an empty directory-entry path");
    }

    int RunMobiHuffmanGuardChild()
    {
        MOBIHuffCdic huffman{};
        huffman.index_count = 1;
        huffman.code_length = 1;
        huffman.table1[0] = 0x80U;

        std::array<uint16_t, 1> symbolOffsets{};
        std::array<unsigned char, 2> zeroLengthSymbol{ 0x80, 0x00 };
        std::array<unsigned char*, 1> symbolTables{ zeroLengthSymbol.data() };
        huffman.symbol_offsets = symbolOffsets.data();
        huffman.symbols = symbolTables.data();

        std::array<unsigned char, 8> compressed{};
        std::array<unsigned char, 16> output{};
        size_t outputLength = output.size();
        return mobi_decompress_huffman(
                   output.data(), compressed.data(), &outputLength,
                   compressed.size(), &huffman) == MOBI_DATA_CORRUPT
            ? 0
            : 1;
    }

    void TestMobiNativeHardening()
    {
        {
            std::array<unsigned char, 16> shortCdic{
                'C', 'D', 'I', 'C',
                0x00, 0x00, 0x00, 0x10,
                0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x01 };
            MOBIPdbRecord record{};
            record.size = shortCdic.size();
            record.data = shortCdic.data();
            MOBIHuffCdic huffman{};

            const MOBI_RET result = mobi_parse_cdic(&huffman, &record, 0);
            const std::unique_ptr<uint16_t, decltype(&free)> offsets{
                huffman.symbol_offsets, &free };
            Require(result == MOBI_DATA_CORRUPT && huffman.symbol_offsets == nullptr,
                    "libmobi allocated CDIC offsets before validating the record bounds");
        }

        {
            std::array<unsigned char, 16> oversizedCdic{
                'C', 'D', 'I', 'C',
                0x00, 0x00, 0x00, 0x10,
                0x02, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x10 };
            MOBIPdbRecord record{};
            record.size = oversizedCdic.size();
            record.data = oversizedCdic.data();
            MOBIHuffCdic huffman{};

            const MOBI_RET result = mobi_parse_cdic(&huffman, &record, 0);
            const std::unique_ptr<uint16_t, decltype(&free)> offsets{
                huffman.symbol_offsets, &free };
            Require(result == MOBI_DATA_CORRUPT && huffman.symbol_offsets == nullptr,
                    "libmobi accepted an excessive CDIC offset table");
        }

        std::vector<wchar_t> executablePath(32768);
        const DWORD executableLength = GetModuleFileNameW(
            nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        Require(executableLength != 0 && executableLength < executablePath.size(),
                "Unable to resolve the test executable for the HUFF guard child");

        const std::wstring application{ executablePath.data(), executableLength };
        std::wstring commandLine =
            L"\"" + application + L"\" --mobi-huffman-guard-child";
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION processInfo{};
        Require(CreateProcessW(
                    application.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                    CREATE_NO_WINDOW, nullptr, nullptr, &startup, &processInfo) != FALSE,
                "Unable to start the libmobi HUFF guard child process");

        const Win32HandleOwner process{ processInfo.hProcess };
        const Win32HandleOwner thread{ processInfo.hThread };
        const DWORD waitResult = WaitForSingleObject(process.Get(), 5000);
        if (waitResult != WAIT_OBJECT_0)
        {
            static_cast<void>(TerminateProcess(process.Get(), 2));
            static_cast<void>(WaitForSingleObject(process.Get(), 5000));
            Fail(waitResult == WAIT_TIMEOUT
                     ? "libmobi hung on a zero-length HUFF code"
                     : "Waiting for the libmobi HUFF guard child failed");
        }

        DWORD exitCode{};
        Require(GetExitCodeProcess(process.Get(), &exitCode) != FALSE && exitCode == 0,
                "libmobi did not reject a zero-length HUFF code");
    }

    void TestLoggerLastErrorPreservation()
    {
        constexpr size_t threadCount = 4;
        constexpr size_t callsPerThread = 16;
        std::atomic<bool> preserved{ true };
        {
            std::vector<std::jthread> workers;
            workers.reserve(threadCount);
            for (size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
            {
                workers.emplace_back([&, threadIndex]
                {
                    const DWORD sentinel = static_cast<DWORD>(
                        0x5A000000UL + threadIndex * 0x100UL + 1UL);
                    for (size_t call = 0; call < callsPerThread; ++call)
                    {
                        SetLastError(sentinel);
                        const std::uint64_t correlationId = Log::NextCorrelationId();
                        if (GetLastError() != sentinel)
                            preserved.store(false, std::memory_order_relaxed);

                        SetLastError(sentinel);
                        Log::Diagnostic(
                            Log::EventId::Logger, S_OK, correlationId,
                            "last-error-regression-test");
                        if (GetLastError() != sentinel)
                            preserved.store(false, std::memory_order_relaxed);
                    }
                });
            }
        }
        Require(preserved.load(std::memory_order_relaxed),
                "Concurrent logger calls changed the calling thread's last-error value");
    }

    void RunSelfTests(Suite& suite)
    {
        suite.Run("archive path normalization and traversal", TestToAbsolutePath);
        suite.Run("IStream exact multi-chunk read and restoration",
                  TestReadIStreamExactReadsAndRestore);
        suite.Run("IStream short/over-reported read rejection",
                  TestReadIStreamFaultsAndRestore);
        suite.Run("IStream exception translation and restoration",
                  TestReadIStreamExceptionTranslation);
        suite.Run("IStream input size limit", TestReadIStreamSizeLimit);
        suite.Run("IStream metadata and extension utilities",
                  TestStreamMetadataUtilities);
        suite.Run("strict numeric parsing", TestTryParseNumberStrictness);
        suite.Run("malformed parser input safety", TestMalformedParserInputs);
        suite.Run("CHMLib native malformed-input guards", TestChmNativeHardening);
        suite.Run("libmobi native malformed-input guards", TestMobiNativeHardening);
        suite.Run("logger noexcept/last-error contract",
                  TestLoggerLastErrorPreservation);

        TempDirectory temporary;
        Fixtures fixtures{};
        if (!suite.Run("fixture generation", [&]
            {
                fixtures = CreateFixtures(temporary.Get());
            }))
            return;

        suite.Run("parser extension routing", TestRouting);
        suite.Run("parser result bitmap ownership", TestResultBitmapOwnership);
        suite.Run("ZIP archive/cache integration", [&] { TestArchiveCache(fixtures.EpubPath); });
        suite.Run("ZIP cache traversal and rewind", [&] { TestRawCacheTraversal(fixtures.EpubPath); });
        suite.Run("RamFile stream snapshot ownership", TestRamFileSnapshot);

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

#if defined(LIVEICONS_TEST_ENABLE_CBR)
        suite.Run("CBR path parsing", [&]
        {
            Parser::Cbr parser;
            auto result = parser.Parse(fixtures.CbrPath.wstring());
            VerifyParseResult(result, std::nullopt, SIZE{ CbrFixtureWidth, CbrFixtureHeight });
        });

        suite.Run("CBR IStream parsing", [&]
        {
            Parser::Cbr parser;
            auto stream = MakeMemoryStream(fixtures.CbrBytes);
            LARGE_INTEGER originalPosition{};
            originalPosition.QuadPart = 17;
            Require(SUCCEEDED(stream.Get()->Seek(originalPosition, STREAM_SEEK_SET, nullptr)),
                    "Unable to position the CBR fixture stream before parsing");
            auto result = parser.Parse(stream.Get());
            VerifyParseResult(result, std::nullopt, SIZE{ CbrFixtureWidth, CbrFixtureHeight });

            constexpr LARGE_INTEGER zero{};
            ULARGE_INTEGER position{};
            Require(SUCCEEDED(stream.Get()->Seek(zero, STREAM_SEEK_CUR, &position)) &&
                        position.QuadPart == static_cast<ULONGLONG>(originalPosition.QuadPart),
                    "CBR parser did not restore the input IStream position");
        });

        suite.Run("solid CBR path parsing", [&]
        {
            Parser::Cbr parser;
            auto result = parser.Parse(fixtures.SolidCbrPath.wstring());
            VerifyParseResult(result, std::nullopt, SIZE{ CbrFixtureWidth, CbrFixtureHeight });
        });

        suite.Run("solid CBR IStream parsing", [&]
        {
            Parser::Cbr parser;
            auto stream = MakeMemoryStream(fixtures.SolidCbrBytes);
            LARGE_INTEGER originalPosition{};
            originalPosition.QuadPart = 23;
            Require(SUCCEEDED(stream.Get()->Seek(originalPosition, STREAM_SEEK_SET, nullptr)),
                    "Unable to position the solid CBR fixture stream before parsing");
            auto result = parser.Parse(stream.Get());
            VerifyParseResult(result, std::nullopt, SIZE{ CbrFixtureWidth, CbrFixtureHeight });

            constexpr LARGE_INTEGER zero{};
            ULARGE_INTEGER position{};
            Require(SUCCEEDED(stream.Get()->Seek(zero, STREAM_SEEK_CUR, &position)) &&
                        position.QuadPart == static_cast<ULONGLONG>(originalPosition.QuadPart),
                    "Solid CBR parser did not restore the input IStream position");
        });

        suite.Run("CBR oversized dictionary rejection", [&]
        {
            Parser::Cbr parser;
            auto stream = MakeMemoryStream(fixtures.OversizedDictionaryCbrBytes);
            LARGE_INTEGER originalPosition{};
            originalPosition.QuadPart = 29;
            Require(SUCCEEDED(stream.Get()->Seek(originalPosition, STREAM_SEEK_SET, nullptr)),
                    "Unable to position the oversized-dictionary CBR stream before parsing");

            auto result = parser.Parse(stream.Get());
            VerifyParseFailure(result);
            Require(result.HResult == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) &&
                        result.Error.find(L"128 MiB") != std::wstring::npos,
                    "CBR oversized-dictionary policy returned the wrong failure");

            constexpr LARGE_INTEGER zero{};
            ULARGE_INTEGER position{};
            Require(SUCCEEDED(stream.Get()->Seek(zero, STREAM_SEEK_CUR, &position)) &&
                        position.QuadPart == static_cast<ULONGLONG>(originalPosition.QuadPart),
                    "Oversized-dictionary CBR parsing did not restore the input stream position");
        });

        suite.Run("multi-volume CBR rejection", [&]
        {
            Parser::Cbr parser;
            auto result = parser.Parse(fixtures.MultiVolumeCbrPath.wstring());
            VerifyParseFailure(result);
            Require(result.HResult == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) &&
                        result.Error.find(L"Multi-volume") != std::wstring::npos,
                    "Multi-volume CBR policy returned the wrong failure");
        });

        suite.Run("invalid CBR path failure", [&]
        {
            Parser::Cbr parser;
            auto result = parser.Parse(fixtures.InvalidCbrPath.wstring());
            VerifyParseFailure(result);
        });

        suite.Run("invalid CBR IStream failure and restoration", [&]
        {
            Parser::Cbr parser;
            auto stream = MakeMemoryStream(fixtures.InvalidCbrBytes);
            LARGE_INTEGER originalPosition{};
            originalPosition.QuadPart = 31;
            Require(SUCCEEDED(stream.Get()->Seek(originalPosition, STREAM_SEEK_SET, nullptr)),
                    "Unable to position the invalid CBR fixture stream before parsing");

            auto result = parser.Parse(stream.Get());
            VerifyParseFailure(result);

            constexpr LARGE_INTEGER zero{};
            ULARGE_INTEGER position{};
            Require(SUCCEEDED(stream.Get()->Seek(zero, STREAM_SEEK_CUR, &position)) &&
                        position.QuadPart == static_cast<ULONGLONG>(originalPosition.QuadPart),
                    "Failed CBR parsing did not restore the input IStream position");
        });
#endif
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
                           "CBR tests were disabled with /p:LiveIconsEnableCbrTests=false");
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

    if (argumentCount == 2 &&
        std::wstring_view{ arguments[1] } == L"--mobi-huffman-guard-child")
        return RunMobiHuffmanGuardChild();

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
