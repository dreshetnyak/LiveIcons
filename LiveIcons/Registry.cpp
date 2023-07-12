#include "pch.h"
#include "Registry.h"

#include "Utility.h"

namespace Registry
{
	HRESULT SetEntry(const Entry& registryEntry)
	{
		constexpr char emptyStr[]{ '\x0' };
		Logger::Write(std::format("Registry::SetEntry: Key Name: '{}'; Value Name: '{}'; Data: '{}'",
			registryEntry.KeyName != nullptr ? StrLib::ToString(registryEntry.KeyName) : emptyStr,
			registryEntry.ValueName != nullptr ? StrLib::ToString(registryEntry.ValueName) : emptyStr,
			registryEntry.Data != nullptr ? StrLib::ToString(registryEntry.Data) : emptyStr));

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
		for (const auto& registryEntry : registryEntries)
		{
			if (const auto result = SetEntry(registryEntry); FAILED(result))
				return result;
		}

		return S_OK;
	}

	HRESULT DeleteRegistryPaths(const std::vector<PCWSTR>& paths)
	{
		constexpr auto fileNotFound = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		for (const auto path : paths)
		{
			constexpr char emptyStr[]{ '\x0' };
			Logger::Write(std::format("Registry::DeleteRegistryPaths: Path: '{}'", path != nullptr ? StrLib::ToString(path) : emptyStr));
			if (const auto result = HRESULT_FROM_WIN32(RegDeleteTreeW(HKEY_CURRENT_USER, path)); result != fileNotFound && FAILED(result))
				return result;
		}

		return S_OK;
	}
}
