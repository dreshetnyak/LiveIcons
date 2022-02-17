#pragma once
#include "Configuration.h"

class ClassFactory final : public IClassFactory
{
	ReferenceCounter InstanceReferences{};

	CreateInstanceFunction Create;
	~ClassFactory();

public:
	static HRESULT CreateInstance(const IID& clsid, const IID& riid, void** ppv);
	explicit ClassFactory(CreateInstanceFunction createInstance);
	ClassFactory() = delete;
	ClassFactory(const ClassFactory& classFactory) = delete;
	ClassFactory(ClassFactory&& classFactory) = delete;
	ClassFactory& operator= (const ClassFactory&) = delete;

	// IUnknown
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
	IFACEMETHODIMP_(ULONG) AddRef() override;
	IFACEMETHODIMP_(ULONG) Release() override;

	// IClassFactory
	IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
	IFACEMETHODIMP LockServer(BOOL isLock) override;
};