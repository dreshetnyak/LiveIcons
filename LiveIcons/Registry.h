#pragma once

#include <span>
#include <Windows.h>

namespace Registry
{
	struct Entry final
	{
		HKEY RootKey;
		PCWSTR KeyName;
		PCWSTR ValueName;
		PCWSTR Data;
	};

	HRESULT SetEntry(const Entry& entry) noexcept;
	HRESULT SetEntries(std::span<const Entry> entries) noexcept;
	HRESULT DeleteRegistryPaths(std::span<const PCWSTR> paths) noexcept;
}
