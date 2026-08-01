#include "pch.h"
#include "DllMain.h"

#include <array>
#include <string>

#include "ClassFactory.h"
#include "Configuration.h"
#include "ExceptionBoundary.h"
#include "Log.h"
#include "Registry.h"

ReferenceCounter dllReferenceCounter{};
ReferenceCounter dllServerLockCounter{};
HINSTANCE dllModuleHandle{};

namespace
{
	[[nodiscard]] HRESULT LastErrorAsHResult() noexcept
	{
		const DWORD error = GetLastError();
		return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
	}

	[[nodiscard]] HRESULT GetModulePath(std::wstring& outPath)
	{
		outPath.clear();
		if (dllModuleHandle == nullptr)
			return E_UNEXPECTED;

		constexpr DWORD MaximumPathLength{ 32768 };
		for (DWORD capacity = MAX_PATH; capacity <= MaximumPathLength; capacity *= 2)
		{
			if (capacity > MaximumPathLength / 2)
				capacity = MaximumPathLength;
			std::wstring path(capacity, L'\0');
			SetLastError(ERROR_SUCCESS);
			const DWORD length = GetModuleFileNameW(
				dllModuleHandle, path.data(), capacity);
			if (length == 0)
				return LastErrorAsHResult();
			if (length < capacity)
			{
				path.resize(length);
				outPath.swap(path);
				return S_OK;
			}
			if (capacity == MaximumPathLength)
				break;
		}

		return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
	}

	[[nodiscard]] HRESULT UnregisterImplementation() noexcept
	{
		constexpr std::array<PCWSTR, 8> paths
		{
			REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR,
			CLSID_EPUB_THUMBNAIL_PROVIDER_PATH,
			CLSID_FB2_THUMBNAIL_PROVIDER_PATH,
			CLSID_MOBI_THUMBNAIL_PROVIDER_PATH,
			CLSID_AZW3_THUMBNAIL_PROVIDER_PATH,
			CLSID_AZW_THUMBNAIL_PROVIDER_PATH,
			CLSID_CHM_THUMBNAIL_PROVIDER_PATH,
			CLSID_CBR_THUMBNAIL_PROVIDER_PATH
		};
		return Registry::DeleteRegistryPaths(paths);
	}

	[[nodiscard]] HRESULT RegisterImplementation(const std::wstring& modulePath) noexcept
	{
		const std::array<Registry::Entry, 10> entries
		{
			Registry::Entry{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR, nullptr, LIVE_ICONS_HANDLER_NAME },
			Registry::Entry{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR REG_INPROCSERVER32, nullptr, modulePath.c_str() },
			Registry::Entry{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR REG_INPROCSERVER32, L"ThreadingModel", L"Apartment" },
			Registry::Entry{ HKEY_CURRENT_USER, CLSID_EPUB_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
			Registry::Entry{ HKEY_CURRENT_USER, CLSID_FB2_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
			Registry::Entry{ HKEY_CURRENT_USER, CLSID_MOBI_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
			Registry::Entry{ HKEY_CURRENT_USER, CLSID_AZW3_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
			Registry::Entry{ HKEY_CURRENT_USER, CLSID_AZW_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
			Registry::Entry{ HKEY_CURRENT_USER, CLSID_CHM_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
			Registry::Entry{ HKEY_CURRENT_USER, CLSID_CBR_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR }
		};
		return Registry::SetEntries(entries);
	}
}

STDAPI_(BOOL) DllMain(
	const HMODULE moduleHandle,
	const DWORD callReason,
	LPVOID) noexcept
{
	if (callReason != DLL_PROCESS_ATTACH)
		return TRUE;
	dllModuleHandle = moduleHandle;
	DisableThreadLibraryCalls(moduleHandle);
	return TRUE;
}

__control_entrypoint(DllExport)
STDAPI DllCanUnloadNow()
{
	return dllReferenceCounter.NoReference() && dllServerLockCounter.NoReference()
		? S_OK
		: S_FALSE;
}

_Check_return_
STDAPI DllGetClassObject(
	_In_ REFCLSID clsid,
	_In_ REFIID riid,
	_Outptr_ void** ppv)
{
	if (ppv == nullptr)
		return E_POINTER;
	*ppv = nullptr;

	const auto correlationId = Log::NextCorrelationId();
	const HRESULT result = ExceptionBoundary::ToHResult(
		Log::EventId::DllGetClassObject,
		correlationId,
		[&]() -> HRESULT
		{
			const HRESULT result = ClassFactory::CreateInstance(clsid, riid, ppv);
			if (FAILED(result) && result != CLASS_E_CLASSNOTAVAILABLE)
				Log::Error(
					Log::EventId::DllGetClassObject,
					result,
					correlationId,
					"activation");
			return result;
		});
	if (FAILED(result))
		return result;
	if (*ppv == nullptr)
	{
		Log::Error(
			Log::EventId::DllGetClassObject,
			E_UNEXPECTED,
			correlationId,
			"activation-null-output");
		return E_UNEXPECTED;
	}
	return result;
}

STDAPI DllUnregisterServer() noexcept
{
	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::DllUnregisterServer,
		correlationId,
		[&]() -> HRESULT
		{
			const HRESULT result = UnregisterImplementation();
			if (FAILED(result))
				Log::Error(
					Log::EventId::DllUnregisterServer,
					result,
					correlationId,
					"registry-delete");
			return result;
		});
}

STDAPI DllRegisterServer() noexcept
{
	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::DllRegisterServer,
		correlationId,
		[&]() -> HRESULT
		{
			std::wstring modulePath;
			if (const HRESULT result = GetModulePath(modulePath); FAILED(result))
			{
				Log::Error(
					Log::EventId::DllRegisterServer,
					result,
					correlationId,
					"module-path");
				return result;
			}

			const HRESULT result = RegisterImplementation(modulePath);
			if (FAILED(result))
			{
				Log::Error(
					Log::EventId::DllRegisterServer,
					result,
					correlationId,
					"registry-write");
				const HRESULT rollbackResult = UnregisterImplementation();
				if (FAILED(rollbackResult))
					Log::Error(
						Log::EventId::DllRegisterServer,
						rollbackResult,
						correlationId,
						"rollback");
				return result;
			}

			SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
			return S_OK;
		});
}
