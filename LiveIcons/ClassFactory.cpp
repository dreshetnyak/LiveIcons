#include "pch.h"
#include "ClassFactory.h"

#include "Configuration.h"
#include "DllMain.h"
#include "ExceptionBoundary.h"
#include "Log.h"

HRESULT ClassFactory::CreateInstance(
	const IID& clsid,
	const IID& riid,
	void** ppv) noexcept
{
	if (ppv == nullptr)
		return E_POINTER;
	*ppv = nullptr;

	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::ClassFactoryCreate,
		correlationId,
		[&]() -> HRESULT
		{
			const auto createInstance = Configuration::GetInstantiatorFunction(clsid);
			if (createInstance == nullptr)
			{
				Log::Diagnostic(
					Log::EventId::ClassFactoryCreate,
					CLASS_E_CLASSNOTAVAILABLE,
					correlationId,
					"class-not-available");
				return CLASS_E_CLASSNOTAVAILABLE;
			}

			auto* const classFactory = new (std::nothrow) ClassFactory{ createInstance };
			if (classFactory == nullptr)
			{
				Log::Error(
					Log::EventId::ClassFactoryCreate,
					E_OUTOFMEMORY,
					correlationId,
					"factory-allocation");
				return E_OUTOFMEMORY;
			}

			const HRESULT result = classFactory->QueryInterface(riid, ppv);
			classFactory->Release();
			if (FAILED(result))
				Log::Error(
					Log::EventId::ClassFactoryCreate,
					result,
					correlationId,
					"factory-query-interface");
			return result;
		});
}

ClassFactory::ClassFactory(const CreateInstanceFunction createInstance) noexcept :
	Create{ createInstance }
{
	static_cast<void>(dllReferenceCounter.Increment());
}

ClassFactory::~ClassFactory() noexcept
{
	static_cast<void>(dllReferenceCounter.Decrement());
}

HRESULT ClassFactory::LockServer(const BOOL isLock) noexcept
{
	if (isLock)
		static_cast<void>(dllServerLockCounter.Increment());
	else
		static_cast<void>(dllServerLockCounter.Decrement());

	Log::Diagnostic(
		Log::EventId::ClassFactoryLockServer,
		S_OK,
		0,
		isLock ? "lock" : "unlock");
	return S_OK;
}

HRESULT ClassFactory::QueryInterface(REFIID riid, void** ppv) noexcept
{
	if (ppv == nullptr)
		return E_POINTER;
	*ppv = nullptr;

	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::ClassFactoryQueryInterface,
		correlationId,
		[&]() -> HRESULT
		{
			static const QITAB interfaces[]
			{
				QITABENT(ClassFactory, IClassFactory),
				{ nullptr, 0 }
			};
			const HRESULT result = QISearch(this, interfaces, riid, ppv);
			if (FAILED(result))
				Log::Diagnostic(
					Log::EventId::ClassFactoryQueryInterface,
					result,
					correlationId,
					"interface-not-supported");
			return result;
		});
}

ULONG ClassFactory::AddRef() noexcept
{
	return ClassFactoryReferences.Increment();
}

ULONG ClassFactory::Release() noexcept
{
	const auto references = ClassFactoryReferences.Decrement();
	if (references == 0)
		delete this;
	return references;
}

HRESULT ClassFactory::CreateInstance(
	IUnknown* outer,
	const IID& riid,
	void** ppv) noexcept
{
	if (ppv == nullptr)
		return E_POINTER;
	*ppv = nullptr;
	if (outer != nullptr)
		return CLASS_E_NOAGGREGATION;
	if (Create == nullptr)
		return E_UNEXPECTED;

	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::ClassFactoryCreate,
		correlationId,
		[&]() -> HRESULT
		{
			const HRESULT result = Create(riid, ppv);
			if (FAILED(result))
				Log::Error(
					Log::EventId::ClassFactoryCreate,
					result,
					correlationId,
					"plugin-activation");
			return result;
		});
}
