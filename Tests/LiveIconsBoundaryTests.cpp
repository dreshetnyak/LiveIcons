#include <Windows.h>
#include <objidl.h>
#include <thumbcache.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <new>
#include <shlobj.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../LiveIcons/Configuration.h"

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void Require(const bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    class DllExports final
    {
        using CanUnloadNowFunction = HRESULT(STDAPICALLTYPE*)();
        using GetClassObjectFunction = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);

        HMODULE Module{};
        CanUnloadNowFunction CanUnloadNowFunctionPointer{};
        GetClassObjectFunction GetClassObjectFunctionPointer{};

        template<typename Function>
        [[nodiscard]] Function Resolve(const char* name)
        {
            const auto address = GetProcAddress(Module, name);
            if (address == nullptr)
                Fail(std::string{ "LiveIcons.dll does not export " } + name);
            return reinterpret_cast<Function>(address);
        }

    public:
        DllExports()
        {
			std::vector<wchar_t> executablePath(32768);
            const DWORD pathLength = GetModuleFileNameW(
                nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
            if (pathLength == 0 || pathLength >= executablePath.size())
                Fail("unable to locate the boundary-test executable");

            std::wstring dllPath{ executablePath.data(), pathLength };
            const auto separator = dllPath.find_last_of(L"\\/");
            if (separator == std::wstring::npos)
                Fail("boundary-test executable path has no parent directory");
            dllPath.replace(separator + 1, std::wstring::npos, L"LiveIcons.dll");

            Module = LoadLibraryExW(
                dllPath.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (Module == nullptr)
                Fail("unable to load LiveIcons.dll from the boundary-test output directory");

            CanUnloadNowFunctionPointer = Resolve<CanUnloadNowFunction>("DllCanUnloadNow");
            GetClassObjectFunctionPointer = Resolve<GetClassObjectFunction>("DllGetClassObject");
        }

        DllExports(const DllExports&) = delete;
        DllExports& operator=(const DllExports&) = delete;

        ~DllExports()
        {
            if (Module != nullptr)
                FreeLibrary(Module);
        }

        [[nodiscard]] HRESULT CanUnloadNow() const noexcept
        {
            return CanUnloadNowFunctionPointer();
        }

        [[nodiscard]] HRESULT GetClassObject(
            REFCLSID classId, REFIID interfaceId, void** object) const noexcept
        {
            return GetClassObjectFunctionPointer(classId, interfaceId, object);
        }
    };

    DllExports& Exports()
    {
        static DllExports exports;
        return exports;
    }

    class Suite final
    {
        std::size_t Passed{};
        std::size_t Failed{};

    public:
        template<typename Function>
        void Run(const char* name, Function&& function)
        {
            try
            {
                std::invoke(std::forward<Function>(function));
                ++Passed;
                std::cout << "[PASS] " << name << '\n';
            }
            catch (const std::exception& exception)
            {
                ++Failed;
                std::cout << "[FAIL] " << name << ": " << exception.what() << '\n';
            }
            catch (...)
            {
                ++Failed;
                std::cout << "[FAIL] " << name << ": unknown exception\n";
            }
        }

        [[nodiscard]] int ExitCode() const noexcept
        {
            return Failed == 0 ? 0 : 1;
        }

        void PrintSummary() const
        {
            std::cout << "\nSummary: " << Passed << " passed, " << Failed << " failed.\n";
        }
    };

    template<typename Interface>
    class ComOwner final
    {
        Interface* Pointer{};

    public:
        ComOwner() = default;
        explicit ComOwner(Interface* pointer) noexcept : Pointer{ pointer } { }
        ComOwner(const ComOwner&) = delete;
        ComOwner& operator=(const ComOwner&) = delete;

        ComOwner(ComOwner&& other) noexcept : Pointer{ std::exchange(other.Pointer, nullptr) } { }

        ComOwner& operator=(ComOwner&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                Pointer = std::exchange(other.Pointer, nullptr);
            }
            return *this;
        }

        ~ComOwner()
        {
            Reset();
        }

        void Reset(Interface* pointer = nullptr) noexcept
        {
            if (Pointer != nullptr)
                Pointer->Release();
            Pointer = pointer;
        }

        [[nodiscard]] Interface* Get() const noexcept { return Pointer; }
        [[nodiscard]] Interface** Put() noexcept
        {
            Reset();
            return &Pointer;
        }

        Interface* operator->() const noexcept { return Pointer; }
    };

    enum class StatBehavior
    {
        ReturnFailure,
        ThrowBadAllocation,
        ThrowStandardException,
        ThrowUnknownException,
        ThrowBadAllocationFromQueryInterface,
        ThrowStandardExceptionFromQueryInterface,
        ThrowUnknownExceptionFromQueryInterface
    };

    constexpr char SensitiveExceptionText[]{ "liveicons-sensitive-test-value-7D5F13C4" };

    class FaultInjectingStream final : public IStream
    {
        std::atomic<ULONG> References{ 1 };
        StatBehavior Behavior;

    public:
        explicit FaultInjectingStream(const StatBehavior behavior) noexcept : Behavior{ behavior } { }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override
        {
            if (Behavior == StatBehavior::ThrowBadAllocationFromQueryInterface)
                throw std::bad_alloc{};
            if (Behavior == StatBehavior::ThrowStandardExceptionFromQueryInterface)
                throw std::runtime_error{ SensitiveExceptionText };
            if (Behavior == StatBehavior::ThrowUnknownExceptionFromQueryInterface)
                throw 11;

            if (object == nullptr)
                return E_POINTER;
            *object = nullptr;
            if (interfaceId != IID_IUnknown && interfaceId != IID_ISequentialStream && interfaceId != IID_IStream)
                return E_NOINTERFACE;
            *object = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return References.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remaining = References.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (remaining == 0)
                delete this;
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE Read(void*, ULONG, ULONG*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }

        HRESULT STDMETHODCALLTYPE Stat(STATSTG*, DWORD) override
        {
            switch (Behavior)
            {
            case StatBehavior::ThrowBadAllocation:
                throw std::bad_alloc{};
            case StatBehavior::ThrowStandardException:
                throw std::runtime_error{ "injected IStream::Stat failure" };
            case StatBehavior::ThrowUnknownException:
                throw 7;
            case StatBehavior::ReturnFailure:
            default:
                return STG_E_READFAULT;
            }
        }

        HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
    };

    ComOwner<IClassFactory> CreateFactory()
    {
        IClassFactory* factory{};
        const HRESULT result = Exports().GetClassObject(
            Configuration::CLSID_LIVE_ICONS_HANDLER,
            IID_IClassFactory,
            reinterpret_cast<void**>(&factory));
        Require(result == S_OK && factory != nullptr, "DllGetClassObject did not return a class factory");
        return ComOwner<IClassFactory>{ factory };
    }

    ComOwner<IUnknown> CreatePlugin(IClassFactory* factory)
    {
        IUnknown* plugin{};
        const HRESULT result = factory->CreateInstance(
            nullptr, IID_IUnknown, reinterpret_cast<void**>(&plugin));
        Require(result == S_OK && plugin != nullptr, "IClassFactory::CreateInstance failed");
        return ComOwner<IUnknown>{ plugin };
    }

    void TestExportContracts()
    {
        Require(Exports().CanUnloadNow() == S_OK, "fresh DLL reported live references");
        Require(Exports().GetClassObject(Configuration::CLSID_LIVE_ICONS_HANDLER, IID_IClassFactory, nullptr) == E_POINTER,
            "DllGetClassObject did not reject a null output pointer");

        void* output = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
        const CLSID unknownClass{ 0x159fc6aa, 0x26c7, 0x4bad, { 0x92, 0xc7, 0x88, 0x53, 0x57, 0xf8, 0xf3, 0x9c } };
        Require(Exports().GetClassObject(unknownClass, IID_IClassFactory, &output) == CLASS_E_CLASSNOTAVAILABLE,
            "unknown CLSID returned the wrong error");
        Require(output == nullptr, "failed DllGetClassObject left a stale output pointer");
        Require(Exports().CanUnloadNow() == S_OK, "failed activation leaked a module reference");
    }

    void TestObjectLifetime()
    {
        auto factory = CreateFactory();
        Require(Exports().CanUnloadNow() == S_FALSE, "live class factory was not counted");

        auto plugin = CreatePlugin(factory.Get());
        factory.Reset();
        Require(Exports().CanUnloadNow() == S_FALSE, "live plugin object was not counted");

        plugin.Reset();
        Require(Exports().CanUnloadNow() == S_OK, "released plugin object leaked a module reference");
    }

    void TestFactoryContracts()
    {
        auto factory = CreateFactory();
#pragma warning(suppress : 6387) // Deliberately violate the COM precondition.
        Require(factory->QueryInterface(IID_IUnknown, nullptr) == E_POINTER,
            "class factory QueryInterface did not reject a null output pointer");
#pragma warning(suppress : 6387) // Deliberately violate the COM precondition.
        Require(factory->CreateInstance(nullptr, IID_IUnknown, nullptr) == E_POINTER,
            "class factory did not reject a null output pointer");

        void* output = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
        auto* const fakeOuter = reinterpret_cast<IUnknown*>(static_cast<std::uintptr_t>(1));
        Require(factory->CreateInstance(fakeOuter, IID_IUnknown, &output) == CLASS_E_NOAGGREGATION,
            "class factory accepted aggregation");
        Require(output == nullptr, "aggregation failure left a stale output pointer");

        Require(factory->LockServer(TRUE) == S_OK, "LockServer(TRUE) failed");
        factory.Reset();
        Require(Exports().CanUnloadNow() == S_FALSE, "server lock was not counted");

        auto unlockFactory = CreateFactory();
        Require(unlockFactory->LockServer(FALSE) == S_OK, "LockServer(FALSE) failed");
        unlockFactory.Reset();
        Require(Exports().CanUnloadNow() == S_OK, "server unlock leaked a module reference");

		auto unmatchedUnlockFactory = CreateFactory();
		auto guardedPlugin = CreatePlugin(unmatchedUnlockFactory.Get());
		Require(unmatchedUnlockFactory->LockServer(FALSE) == S_OK,
			"unmatched LockServer(FALSE) failed");
		unmatchedUnlockFactory.Reset();
		Require(Exports().CanUnloadNow() == S_FALSE,
			"unmatched server unlock corrupted the live-object count");
		guardedPlugin.Reset();
		Require(Exports().CanUnloadNow() == S_OK,
			"unmatched server unlock left the DLL locked");
    }

    void TestPluginContracts()
    {
        auto factory = CreateFactory();
        auto plugin = CreatePlugin(factory.Get());
#pragma warning(suppress : 6387) // Deliberately violate the COM precondition.
        Require(plugin->QueryInterface(IID_IUnknown, nullptr) == E_POINTER,
            "plugin QueryInterface did not reject a null output pointer");

        ComOwner<IInitializeWithStream> initializer;
        Require(plugin->QueryInterface(IID_IInitializeWithStream,
                    reinterpret_cast<void**>(initializer.Put())) == S_OK,
            "plugin does not expose IInitializeWithStream");
#pragma warning(suppress : 6387) // Deliberately violate the COM precondition.
        Require(initializer->Initialize(nullptr, STGM_READ) == E_POINTER,
            "Initialize did not reject a null stream");

        ComOwner<IThumbnailProvider> thumbnailProvider;
        Require(plugin->QueryInterface(IID_IThumbnailProvider,
                    reinterpret_cast<void**>(thumbnailProvider.Put())) == S_OK,
            "plugin does not expose IThumbnailProvider");

        HBITMAP bitmap = reinterpret_cast<HBITMAP>(static_cast<std::uintptr_t>(1));
#pragma warning(suppress : 6387) // Deliberately violate the COM precondition.
        Require(thumbnailProvider->GetThumbnail(256, &bitmap, nullptr) == E_POINTER,
            "GetThumbnail did not reject a null alpha output");
        Require(bitmap == nullptr,
            "GetThumbnail did not clear its valid bitmap output when alpha was null");

        WTS_ALPHATYPE alpha = WTSAT_ARGB;
#pragma warning(suppress : 6387) // Deliberately violate the COM precondition.
        Require(thumbnailProvider->GetThumbnail(256, nullptr, &alpha) == E_POINTER,
            "GetThumbnail did not reject a null bitmap output");
        Require(alpha == WTSAT_UNKNOWN,
            "GetThumbnail did not clear its valid alpha output when bitmap was null");
    }

    HRESULT InvokeInitializeWithFault(const StatBehavior behavior)
    {
        auto factory = CreateFactory();
        auto plugin = CreatePlugin(factory.Get());
        ComOwner<IInitializeWithStream> initializer;
        Require(plugin->QueryInterface(IID_IInitializeWithStream,
                    reinterpret_cast<void**>(initializer.Put())) == S_OK,
            "plugin does not expose IInitializeWithStream");

        auto* stream = new FaultInjectingStream{ behavior };
        const HRESULT result = initializer->Initialize(stream, STGM_READ);
        stream->Release();
        return result;
    }

    HRESULT InvokeThumbnailWithFault(const StatBehavior behavior)
    {
        auto factory = CreateFactory();
        auto plugin = CreatePlugin(factory.Get());

        ComOwner<IInitializeWithStream> initializer;
        Require(plugin->QueryInterface(IID_IInitializeWithStream,
                    reinterpret_cast<void**>(initializer.Put())) == S_OK,
            "plugin does not expose IInitializeWithStream");

        ComOwner<IThumbnailProvider> thumbnailProvider;
        Require(plugin->QueryInterface(IID_IThumbnailProvider,
                    reinterpret_cast<void**>(thumbnailProvider.Put())) == S_OK,
            "plugin does not expose IThumbnailProvider");

        auto* stream = new FaultInjectingStream{ behavior };
        const HRESULT initializeResult = initializer->Initialize(stream, STGM_READ);
        stream->Release();
        Require(initializeResult == S_OK, "plugin initialization failed");

        HBITMAP bitmap = reinterpret_cast<HBITMAP>(static_cast<std::uintptr_t>(1));
        WTS_ALPHATYPE alpha = WTSAT_ARGB;
        const HRESULT result = thumbnailProvider->GetThumbnail(256, &bitmap, &alpha);
        Require(bitmap == nullptr, "failed thumbnail call returned a stale bitmap");
        Require(alpha == WTSAT_UNKNOWN, "failed thumbnail call returned a stale alpha type");
        return result;
    }

    void TestExceptionFirewall()
    {
        Require(InvokeInitializeWithFault(StatBehavior::ThrowBadAllocationFromQueryInterface) == E_OUTOFMEMORY,
            "Initialize did not map std::bad_alloc to E_OUTOFMEMORY");
        Require(InvokeInitializeWithFault(StatBehavior::ThrowStandardExceptionFromQueryInterface) == E_UNEXPECTED,
            "Initialize did not map std::exception to E_UNEXPECTED");
        Require(InvokeInitializeWithFault(StatBehavior::ThrowUnknownExceptionFromQueryInterface) == E_UNEXPECTED,
            "Initialize did not map an unknown exception to E_UNEXPECTED");
        Require(InvokeThumbnailWithFault(StatBehavior::ThrowBadAllocation) == E_OUTOFMEMORY,
            "std::bad_alloc was not mapped to E_OUTOFMEMORY");
        Require(InvokeThumbnailWithFault(StatBehavior::ThrowStandardException) == E_UNEXPECTED,
            "std::exception was not mapped to E_UNEXPECTED");
        Require(InvokeThumbnailWithFault(StatBehavior::ThrowUnknownException) == E_UNEXPECTED,
            "unknown exception was not mapped to E_UNEXPECTED");
        Require(InvokeThumbnailWithFault(StatBehavior::ReturnFailure) == STG_E_READFAULT,
            "IStream HRESULT was not preserved");
        Require(Exports().CanUnloadNow() == S_OK, "faulted calls leaked module references");
    }

    void TestConcurrentRelease()
    {
        auto factory = CreateFactory();
        auto plugin = CreatePlugin(factory.Get());
        factory.Reset();

        constexpr std::size_t workerCount = 32;
        for (std::size_t index = 0; index < workerCount; ++index)
            plugin->AddRef();

        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t index = 0; index < workerCount; ++index)
            workers.emplace_back([pointer = plugin.Get()] { pointer->Release(); });
        for (auto& worker : workers)
            worker.join();

        Require(Exports().CanUnloadNow() == S_FALSE, "concurrent Release deleted the guarded object");
        plugin.Reset();
        Require(Exports().CanUnloadNow() == S_OK, "concurrent Release leaked a module reference");
    }

	std::wstring CurrentProcessLogPath()
	{
		PWSTR localAppDataLow{};
		const HRESULT result = SHGetKnownFolderPath(
			FOLDERID_LocalAppDataLow, KF_FLAG_DEFAULT, nullptr, &localAppDataLow);
		if (FAILED(result) || localAppDataLow == nullptr)
			Fail("unable to resolve LocalLow for logger verification");

		std::wstring path{ localAppDataLow };
		CoTaskMemFree(localAppDataLow);
		path += L"\\LiveIcons\\Logs\\LiveIcons-";
		path += std::to_wstring(GetCurrentProcessId());
		path += L".log";
		return path;
	}

	std::string ReadCurrentProcessLog()
	{
		std::ifstream stream{ CurrentProcessLogPath(), std::ios::binary };
		if (!stream)
			Fail("LiveIcons did not create its process log in LocalLow");
		return std::string{
			std::istreambuf_iterator<char>{ stream },
			std::istreambuf_iterator<char>{} };
	}

	void TestLoggerPrivacyAndRateLimit()
	{
		Require(InvokeInitializeWithFault(
			StatBehavior::ThrowStandardExceptionFromQueryInterface) == E_UNEXPECTED,
			"logger privacy probe did not cross the exception boundary");
		auto content = ReadCurrentProcessLog();
		Require(content.find(SensitiveExceptionText) == std::string::npos,
			"logger persisted exception.what() content");
		Require(content.find("detail=\"std_exception\"") != std::string::npos,
			"logger did not persist the static exception category");

		for (std::size_t index = 0; index < 125; ++index)
			static_cast<void>(InvokeInitializeWithFault(
				StatBehavior::ThrowUnknownExceptionFromQueryInterface));
		content = ReadCurrentProcessLog();
		Require(content.find("detail=\"rate-limit-active\"") != std::string::npos,
			"logger did not emit its rate-limit marker");
	}
}

int main()
{
    Suite suite;
    suite.Run("DLL export contracts", TestExportContracts);
    suite.Run("COM object/module lifetime", TestObjectLifetime);
    suite.Run("class factory contracts and server lock", TestFactoryContracts);
    suite.Run("plugin COM output contracts", TestPluginContracts);
    suite.Run("COM exception firewall and output reset", TestExceptionFirewall);
	suite.Run("logger privacy and rate limiting", TestLoggerPrivacyAndRateLimit);
    suite.Run("concurrent AddRef/Release", TestConcurrentRelease);
    suite.PrintSummary();
    return suite.ExitCode();
}
