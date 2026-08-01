#pragma once
#include "Configuration.h"

class ClassFactory final : public IClassFactory
{
	ReferenceCounter ClassFactoryReferences{ 1 };

	CreateInstanceFunction Create;
	~ClassFactory() noexcept;

public:
	static HRESULT CreateInstance(const IID& clsid, const IID& riid, void** ppv) noexcept;
	explicit ClassFactory(CreateInstanceFunction createInstance) noexcept;
	ClassFactory() = delete;
	ClassFactory(const ClassFactory& classFactory) = delete;
	ClassFactory(ClassFactory&& classFactory) = delete;
	ClassFactory& operator= (const ClassFactory&) = delete;

	// IUnknown
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override;
	IFACEMETHODIMP_(ULONG) AddRef() noexcept override;
	IFACEMETHODIMP_(ULONG) Release() noexcept override;

	// IClassFactory
	IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) noexcept override;
	IFACEMETHODIMP LockServer(BOOL isLock) noexcept override;
};
