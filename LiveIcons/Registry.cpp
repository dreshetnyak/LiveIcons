#include "pch.h"
#include "Registry.h"

namespace Registry
{
	HRESULT SetEntry(const Entry& registryEntry)
	{
		HKEY hKey;
		auto result = HRESULT_FROM_WIN32(RegCreateKeyExW(registryEntry.HKeyRoot, registryEntry.KeyName, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr));
		if (FAILED(result))
			return result;

		result = HRESULT_FROM_WIN32(RegSetValueExW(
			hKey,
			registryEntry.ValueName,
			0,
			REG_SZ,
			reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(registryEntry.Data)),
			(static_cast<DWORD>(wcslen(registryEntry.Data)) + 1) * sizeof(WCHAR)));

		RegCloseKey(hKey);
		return result;
	}

	HRESULT SetEntries(const std::vector<Entry>& registryEntries)
	{		
		for (auto entry = registryEntries.begin(); entry != registryEntries.end(); ++entry)
		{
			if (const auto result = Registry::SetEntry(*entry); FAILED(result))
				return result;
		}

		return S_OK;
	}

	HRESULT DeleteRegistryPaths(const std::vector<PCWSTR>& paths)
	{
		constexpr auto fileNotFound = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		for (auto path = paths.begin(); path != paths.end(); ++path)
		{
			if (const auto result = HRESULT_FROM_WIN32(RegDeleteTreeW(HKEY_CURRENT_USER, *path)); result != fileNotFound && FAILED(result))
				return result;
		}

		return S_OK;
	}
}