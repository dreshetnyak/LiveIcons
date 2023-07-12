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
	Logger::Write("DllMain: " + GetCallReasonName(callReason));
	if (callReason != DLL_PROCESS_ATTACH)
		return TRUE;	
	dllModuleHandle = moduleHandle;
	DisableThreadLibraryCalls(moduleHandle);
	return TRUE;
}

STDAPI DllCanUnloadNow()
{
	Logger::Write("DllCanUnloadNow.");
	return dllReferenceCounter.NoReference()
		? S_OK
		: S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{	
	Logger::Write("DllGetClassObject: Starting.");
	const auto result = ClassFactory::CreateInstance(clsid, riid, ppv);
	Logger::Write(std::format("DllGetClassObject: Finished. HRESULT: {}", std::system_category().message(result)));
	return result;
}

STDAPI DllUnregisterServer()
{
	Logger::Write("DllUnregisterServer: Starting.");
	const auto result = Registry::DeleteRegistryPaths(std::vector
		{
			REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR,
			CLSID_EPUB_THUMBNAIL_PROVIDER_PATH,
			CLSID_FB2_THUMBNAIL_PROVIDER_PATH,
			CLSID_MOBI_THUMBNAIL_PROVIDER_PATH,
			CLSID_AZW3_THUMBNAIL_PROVIDER_PATH,
			CLSID_AZW_THUMBNAIL_PROVIDER_PATH,
			CLSID_CHM_THUMBNAIL_PROVIDER_PATH
		});

	Logger::Write(std::format("DllUnregisterServer: Finished. HRESULT: {}", std::system_category().message(result)));
	return result;
}

STDAPI DllRegisterServer()
{
	Logger::Write("DllRegisterServer: Starting.");

	WCHAR szModuleName[MAX_PATH];
	if (!GetModuleFileNameW(dllModuleHandle, szModuleName, ARRAYSIZE(szModuleName)))
	{
		Logger::Write("DllRegisterServer: Error: GetModuleFileNameW failed.");
		return HRESULT_FROM_WIN32(GetLastError());
	}
		
	const auto result = Registry::SetEntries(std::vector<Registry::Entry>
	{	// RootKey, KeyName, ValueName, Data
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR, nullptr, LIVE_ICONS_HANDLER_NAME },
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR REG_INPROCSERVER32, nullptr, szModuleName },
		{ HKEY_CURRENT_USER, REG_SOFTWARE_CLASSES_CLSID CLSID_LIVE_ICONS_HANDLER_STR REG_INPROCSERVER32, L"ThreadingModel", L"Apartment" },
		{ HKEY_CURRENT_USER, CLSID_EPUB_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
		{ HKEY_CURRENT_USER, CLSID_FB2_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
		{ HKEY_CURRENT_USER, CLSID_MOBI_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
		{ HKEY_CURRENT_USER, CLSID_AZW3_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
		{ HKEY_CURRENT_USER, CLSID_AZW_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR },
		{ HKEY_CURRENT_USER, CLSID_CHM_THUMBNAIL_PROVIDER_PATH, nullptr, CLSID_LIVE_ICONS_HANDLER_STR }
	});
	
	if (SUCCEEDED(result))
	{
		Logger::Write("DllRegisterServer: SHChangeNotify.");
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); // Invalidate the thumbnail cache.
	}
	else
	{
		Logger::Write("DllRegisterServer: DllUnregisterServer.");
		DllUnregisterServer();
	}

	Logger::Write("DllRegisterServer: Finished.");
	return result;
}