#include "pch.h"
#include "ClassFactory.h"
#include "Configuration.h"
#include "DllMain.h"
#include "Utility.h"

HRESULT ClassFactory::CreateInstance(const IID& clsid, const IID& riid, void** ppv)
{
	try
	{
		Log::Write("ClassFactory::CreateInstance: Static Starting.");
		*ppv = nullptr;

		const auto createInstance = Configuration::GetInstantiatorFunction(clsid);
		if (createInstance == nullptr)
		{
			Log::Write("ClassFactory::CreateInstance: Error: CLASS_E_CLASSNOTAVAILABLE");
			return CLASS_E_CLASSNOTAVAILABLE;
		}

		IClassFactory* classFactory = new(std::nothrow) ClassFactory(createInstance);
		if (classFactory == nullptr)
		{
			Log::Write("ClassFactory::CreateInstance: Error: E_OUTOFMEMORY");
			return E_OUTOFMEMORY;
		}

		const auto result = classFactory->QueryInterface(riid, ppv);
		classFactory->Release();
		Log::Write(std::format("ClassFactory::CreateInstance: Static Finished. HRESULT: {}", std::system_category().message(result)));
		return result; // S_OK		
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("ClassFactory::LockServer: Static Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

ClassFactory::ClassFactory(CreateInstanceFunction createInstance): Create(createInstance)
{
	try
	{
		Log::Write("ClassFactory::ClassFactory: Constructor.");
		const auto instanceReferences = InstanceReferences.Increment();
		Log::Write(std::format("ClassFactory::ClassFactory: InstanceReferences.Increment: {}", instanceReferences));
		const auto dllRefCounter = dllReferenceCounter.Increment();
		Log::Write(std::format("ClassFactory::ClassFactory: dllReferenceCounter.Increment: {}", dllRefCounter));
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("ClassFactory::ClassFactory: Exception: '{}'", message != nullptr ? message : ""));
	}
}

ClassFactory::~ClassFactory()
{
	try
	{
		Log::Write("ClassFactory::~ClassFactory: Destructor.");
		const auto instanceReferences = InstanceReferences.Decrement();
		Log::Write(std::format("ClassFactory::~ClassFactory: InstanceReferences.Decrement: {}", instanceReferences));
		const auto dllRefCounter = dllReferenceCounter.Decrement();
		Log::Write(std::format("ClassFactory::~ClassFactory: dllReferenceCounter.Decrement: {}", dllRefCounter));
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("ClassFactory::~ClassFactory: Exception: '{}'", message != nullptr ? message : ""));
	}
}

HRESULT ClassFactory::LockServer(const BOOL isLock)
{
	try
	{
		Log::Write("ClassFactory::LockServer.");
		if (isLock)
		{
			const auto dllRefCounter = dllReferenceCounter.Increment();
			Log::Write(std::format("ClassFactory::LockServer: dllReferenceCounter.Increment: {}", dllRefCounter));
		}
		else
		{
			const auto dllRefCounter = dllReferenceCounter.Decrement();
			Log::Write(std::format("ClassFactory::LockServer: dllReferenceCounter.Decrement: {}", dllRefCounter));
		}
		return S_OK;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("ClassFactory::LockServer: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

HRESULT ClassFactory::QueryInterface(REFIID riid, void** ppv)
{
	try
	{
		Log::Write("ClassFactory::QueryInterface: Starting.");
		static const QITAB QIT[] = { QITABENT(ClassFactory, IClassFactory), { nullptr, 0 } };

		const auto result = QISearch(this, QIT, riid, ppv);
		Log::Write(std::format("ClassFactory::QueryInterface: Finished. HRESULT: {}", std::system_category().message(result)));
		return result;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("ClassFactory::QueryInterface: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

ULONG ClassFactory::AddRef()
{
	try
	{
		const auto instanceReferences = InstanceReferences.Increment();
		Log::Write(std::format("ClassFactory::AddRef: InstanceReferences.Increment: {}", instanceReferences));
		return instanceReferences;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("ClassFactory::AddRef: Exception: '{}'", message != nullptr ? message : ""));
		return 0;
	}
}

ULONG ClassFactory::Release()
{
	try
	{
		const auto refCount = InstanceReferences.Decrement();
		Log::Write(std::format("ClassFactory::Release: InstanceReferences.Decrement: {}", refCount));
		if (InstanceReferences.NoReference())
		{
			Log::Write("ClassFactory::Release: delete this.");
			delete this;
		}
		return refCount;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("ClassFactory::Release: Exception: '{}'", message != nullptr ? message : ""));
		return 0;
	}
}

HRESULT ClassFactory::CreateInstance(IUnknown* pUnkOuter, const IID& riid, void** ppv)
{
	try
	{
		Log::Write("ClassFactory::CreateInstance: Starting.");
		const auto result = pUnkOuter
			? CLASS_E_NOAGGREGATION
			: Create(riid, ppv);

		Log::Write(std::format("ClassFactory::CreateInstance: Finished. HRESULT: {}", std::system_category().message(result)));
		return result;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("ClassFactory::CreateInstance: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}