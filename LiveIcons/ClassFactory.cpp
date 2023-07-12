#include "pch.h"
#include "ClassFactory.h"
#include "Configuration.h"
#include "DllMain.h"
#include "Utility.h"

HRESULT ClassFactory::CreateInstance(const IID& clsid, const IID& riid, void** ppv)
{
	try
	{
		Logger::Write("ClassFactory::CreateInstance: Static Starting.");
		*ppv = nullptr;

		const auto createInstance = Configuration::GetInstantiatorFunction(clsid);
		if (createInstance == nullptr)
		{
			Logger::Write("ClassFactory::CreateInstance: Error: CLASS_E_CLASSNOTAVAILABLE");
			return CLASS_E_CLASSNOTAVAILABLE;
		}

		IClassFactory* classFactory = new(std::nothrow) ClassFactory(createInstance);
		if (classFactory == nullptr)
		{
			Logger::Write("ClassFactory::CreateInstance: Error: E_OUTOFMEMORY");
			return E_OUTOFMEMORY;
		}

		const auto result = classFactory->QueryInterface(riid, ppv);
		classFactory->Release();
		Logger::Write(std::format("ClassFactory::CreateInstance: Static Finished. HRESULT: {}", std::system_category().message(result)));
		return result; // S_OK		
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Logger::Write(std::format("ClassFactory::LockServer: Static Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

ClassFactory::ClassFactory(CreateInstanceFunction createInstance): Create(createInstance)
{
	try
	{
		Logger::Write("ClassFactory::ClassFactory: Constructor.");
		const auto instanceReferences = ClassFactoryReferences.Increment();
		Logger::Write(std::format("ClassFactory::ClassFactory: InstanceReferences.Increment: {}", instanceReferences));
		const auto dllRefCounter = dllReferenceCounter.Increment();
		Logger::Write(std::format("ClassFactory::ClassFactory: dllReferenceCounter.Increment: {}", dllRefCounter));
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Logger::Write(std::format("ClassFactory::ClassFactory: Exception: '{}'", message != nullptr ? message : ""));
	}
}

ClassFactory::~ClassFactory()
{
	try
	{
		Logger::Write("ClassFactory::~ClassFactory: Destructor.");
		const auto instanceReferences = ClassFactoryReferences.Decrement();
		Logger::Write(std::format("ClassFactory::~ClassFactory: InstanceReferences.Decrement: {}", instanceReferences));
		const auto dllRefCounter = dllReferenceCounter.Decrement();
		Logger::Write(std::format("ClassFactory::~ClassFactory: dllReferenceCounter.Decrement: {}", dllRefCounter));
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Logger::Write(std::format("ClassFactory::~ClassFactory: Exception: '{}'", message != nullptr ? message : ""));
	}
}

HRESULT ClassFactory::LockServer(const BOOL isLock)
{
	try
	{
		Logger::Write("ClassFactory::LockServer.");
		if (isLock)
		{
			const auto dllRefCounter = dllReferenceCounter.Increment();
			Logger::Write(std::format("ClassFactory::LockServer: dllReferenceCounter.Increment: {}", dllRefCounter));
		}
		else
		{
			const auto dllRefCounter = dllReferenceCounter.Decrement();
			Logger::Write(std::format("ClassFactory::LockServer: dllReferenceCounter.Decrement: {}", dllRefCounter));
		}
		return S_OK;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Logger::Write(std::format("ClassFactory::LockServer: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

HRESULT ClassFactory::QueryInterface(REFIID riid, void** ppv)
{
	try
	{
		Logger::Write("ClassFactory::QueryInterface: Starting.");
		static const QITAB QIT[] = { QITABENT(ClassFactory, IClassFactory), { nullptr, 0 } };

		const auto result = QISearch(this, QIT, riid, ppv);
		Logger::Write(std::format("ClassFactory::QueryInterface: Finished. HRESULT: {}", std::system_category().message(result)));
		return result;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Logger::Write(std::format("ClassFactory::QueryInterface: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

ULONG ClassFactory::AddRef()
{
	try
	{
		const auto instanceReferences = ClassFactoryReferences.Increment();
		Logger::Write(std::format("ClassFactory::AddRef: ClassFactoryReferences.Increment: {}", instanceReferences));
		return instanceReferences;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Logger::Write(std::format("ClassFactory::AddRef: Exception: '{}'", message != nullptr ? message : ""));
		return 0;
	}
}

ULONG ClassFactory::Release()
{
	try
	{
		const auto refCount = ClassFactoryReferences.Decrement();
		Logger::Write(std::format("ClassFactory::Release: InstanceReferences.Decrement: {}", refCount));
		if (ClassFactoryReferences.NoReference())
		{
			Logger::Write("ClassFactory::Release: delete this.");
			delete this;
		}
		return refCount;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Logger::Write(std::format("ClassFactory::Release: Exception: '{}'", message != nullptr ? message : ""));
		return 0;
	}
}

HRESULT ClassFactory::CreateInstance(IUnknown* pUnkOuter, const IID& riid, void** ppv)
{
	try
	{
		Logger::Write("ClassFactory::CreateInstance: Starting.");
		const auto result = pUnkOuter
			? CLASS_E_NOAGGREGATION
			: Create(riid, ppv);

		Logger::Write(std::format("ClassFactory::CreateInstance: Finished. HRESULT: {}", std::system_category().message(result)));
		return result;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Logger::Write(std::format("ClassFactory::CreateInstance: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}