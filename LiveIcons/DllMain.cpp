// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "DllMain.h"

#include "ClassFactory.h"
#include "ReferenceCounter.h"
#include "Registry.h"
#include "Utility.h"

ReferenceCounter dllReferenceCounter{};
HINSTANCE dllModuleHandle{};

std::string GetCallReasonName(const DWORD callReason)
{
	switch (callReason)
	{
	case DLL_PROCESS_ATTACH: return "DLL_PROCESS_ATTACH";
	case DLL_THREAD_ATTACH: return "DLL_THREAD_ATTACH";
	case DLL_THREAD_DETACH: return "DLL_THREAD_DETACH";
	case DLL_PROCESS_DETACH: return "DLL_PROCESS_DETACH";
	default: return "UNKNOWN";
	}
}

STDAPI_(BOOL) DllMain(const HMODULE moduleHandle, const DWORD callReason, LPVOID)
{
	Log::Write("DllMain: " + GetCallReasonName(callReason));
	if (callReason != DLL_PROCESS_ATTACH)
		return TRUE;	
	dllModuleHandle = moduleHandle;
	DisableThreadLibraryCalls(moduleHandle);
	return TRUE;
}

STDAPI DllCanUnloadNow()
{
	Log::Write("DllCanUnloadNow.");

	return dllReferenceCounter.NoReference()
		? S_OK
		: S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{	
	Log::Write("DllGetClassObject: Starting.");
	const auto result = ClassFactory::CreateInstance(clsid, riid, ppv);
	Log::Write(std::format("DllGetClassObject: Finished. HRESULT: {}", std::system_category().message(result)));
	return result;
}

STDAPI DllUnregisterServer()
{
	Log::Write("DllUnregisterServer: Starting.");

	const auto result = Registry::DeleteRegistryPaths(std::vector
		{
			REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR,
			CLSID_EPUB_THUMBNAIL_PROVIDER_PATH,
			CLSID_FB2_THUMBNAIL_PROVIDER_PATH
		});

	Log::Write(std::format("DllUnregisterServer: Finished. HRESULT: {}", std::system_category().message(result)));	
	return result;
}

STDAPI DllRegisterServer()
{
	Log::Write("DllRegisterServer: Starting.");

	WCHAR szModuleName[MAX_PATH];
	if (!GetModuleFileNameW(dllModuleHandle, szModuleName, ARRAYSIZE(szModuleName)))
	{
		Log::Write("DllRegisterServer: Error: GetModuleFileNameW failed.");
		return HRESULT_FROM_WIN32(GetLastError());
	}
		
	const auto result = Registry::SetEntries(std::vector<Registry::Entry>
	{	// RootKey, KeyName, ValueName, Data
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR, nullptr, LIVE_ICONS_HANDLER_NAME },
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR REG_INPROCSERVER32, nullptr, szModuleName },
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR REG_INPROCSERVER32, L"ThreadingModel", L"Apartment" },
		{ HKEY_CURRENT_USER, CLSID_EPUB_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
		{ HKEY_CURRENT_USER, CLSID_FB2_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR }
	});
	
	if (SUCCEEDED(result))
	{
		Log::Write("DllRegisterServer: SHChangeNotify.");
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); // Invalidate the thumbnail cache.
	}
	else
	{
		Log::Write("DllRegisterServer: DllUnregisterServer.");
		DllUnregisterServer();
	}

	Log::Write("DllRegisterServer: Finished.");
	return result;
}
