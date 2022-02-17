#include "pch.h"
#include "ClassFactory.h"
#include "Configuration.h"
#include "DllMain.h"

HRESULT ClassFactory::CreateInstance(const IID& clsid, const IID& riid, void** ppv)
{
	*ppv = nullptr;

	const auto createInstance = Configuration::GetInstantiatorFunction(clsid);
	if (createInstance == nullptr)
		return CLASS_E_CLASSNOTAVAILABLE;
	
	IClassFactory* classFactory = new(std::nothrow) ClassFactory(createInstance);
	if (classFactory == nullptr)
		return E_OUTOFMEMORY;

	const auto result = classFactory->QueryInterface(riid, ppv);
	classFactory->Release();
	return result; // S_OK
}

ClassFactory::ClassFactory(CreateInstanceFunction createInstance): Create(createInstance)
{
	InstanceReferences.Increment();
	dllReferenceCounter.Increment();
}

ClassFactory::~ClassFactory()
{
	InstanceReferences.Decrement();
	dllReferenceCounter.Decrement();
}

HRESULT ClassFactory::LockServer(const BOOL isLock)
{
	if (isLock)
		dllReferenceCounter.Increment();
	else
		dllReferenceCounter.Decrement();
	return S_OK;
}

HRESULT ClassFactory::QueryInterface(REFIID riid, void** ppv)
{
	static const QITAB QIT[] = { QITABENT(ClassFactory, IClassFactory), { nullptr, 0 } };
	return QISearch(this, QIT, riid, ppv);
}

ULONG ClassFactory::AddRef()
{
	return InstanceReferences.Increment();
}

ULONG ClassFactory::Release()
{
	const auto refCount = InstanceReferences.Decrement();
	if (InstanceReferences.NoReference())
		delete this;
	return refCount;
}

HRESULT ClassFactory::CreateInstance(IUnknown* pUnkOuter, const IID& riid, void** ppv)
{
	return pUnkOuter
		? CLASS_E_NOAGGREGATION
		: Create(riid, ppv);
}