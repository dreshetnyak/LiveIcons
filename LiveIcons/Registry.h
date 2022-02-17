#pragma once
namespace Registry
{
	struct Entry
	{
		HKEY HKeyRoot;
		PCWSTR KeyName;
		PCWSTR ValueName;
		PCWSTR Data;
	};

	HRESULT SetEntry(const Entry& registryEntry);
	HRESULT SetEntries(const std::vector<Entry>& registryEntries);
	HRESULT DeleteRegistryPaths(const std::vector<PCWSTR>& paths);
}

