// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "DllMain.h"

#include "ClassFactory.h"
#include "ReferenceCounter.h"
#include "Registry.h"

ReferenceCounter dllReferenceCounter{};
HINSTANCE dllModuleHandle{};

STDAPI_(BOOL) DllMain(const HMODULE moduleHandle, const DWORD callReason, LPVOID)
{
	if (callReason != DLL_PROCESS_ATTACH)
		return TRUE;	
	dllModuleHandle = moduleHandle;
	DisableThreadLibraryCalls(moduleHandle);
	return TRUE;
}

STDAPI DllCanUnloadNow()
{
	return dllReferenceCounter.NoReference()
		? S_OK
		: S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{	
    return ClassFactory::CreateInstance(clsid, riid, ppv);
}

STDAPI DllUnregisterServer()
{
	return Registry::DeleteRegistryPaths(std::vector
		{
			REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR,
			CLSID_I_THUMBNAIL_PROVIDER_PATH
		});
}

STDAPI DllRegisterServer()
{
	WCHAR szModuleName[MAX_PATH];
	if (!GetModuleFileNameW(dllModuleHandle, szModuleName, ARRAYSIZE(szModuleName)))
		return HRESULT_FROM_WIN32(GetLastError());
		
	const auto result = Registry::SetEntries(std::vector<Registry::Entry>
	{	// RootKey, KeyName, ValueName, Data
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR, nullptr, LIVE_ICONS_HANDLER_NAME },
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR REG_INPROCSERVER32, nullptr, szModuleName },
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR REG_INPROCSERVER32, L"ThreadingModel", L"Apartment" },
		{ HKEY_CURRENT_USER, CLSID_I_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR }
	});
	
	if (SUCCEEDED(result))
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); // This tells the shell to invalidate the thumbnail cache.
	else
		DllUnregisterServer();

	return result;
}
