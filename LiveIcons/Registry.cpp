#include "pch.h"
#include "Registry.h"

#include <limits>

namespace
{
	class RegistryKey final
	{
		HKEY Key{};

	public:
		RegistryKey() noexcept = default;
		RegistryKey(const RegistryKey&) = delete;
		RegistryKey& operator=(const RegistryKey&) = delete;
		~RegistryKey() noexcept
		{
			if (Key != nullptr)
				RegCloseKey(Key);
		}

		[[nodiscard]] HKEY Get() const noexcept { return Key; }
		[[nodiscard]] HKEY* Put() noexcept { return &Key; }
	};

	[[nodiscard]] HRESULT FromRegistryStatus(const LSTATUS status) noexcept
	{
		return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
	}
}

namespace Registry
{
	HRESULT SetEntry(const Entry& entry) noexcept
	{
		if (entry.RootKey == nullptr || entry.KeyName == nullptr || entry.Data == nullptr)
			return E_INVALIDARG;

		const std::size_t characterCount = std::wcslen(entry.Data) + 1;
		if (characterCount > (std::numeric_limits<DWORD>::max)() / sizeof(wchar_t))
			return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

		RegistryKey key;
		const LSTATUS createResult = RegCreateKeyExW(
			entry.RootKey,
			entry.KeyName,
			0,
			nullptr,
			REG_OPTION_NON_VOLATILE,
			KEY_SET_VALUE,
			nullptr,
			key.Put(),
			nullptr);
		if (createResult != ERROR_SUCCESS)
			return FromRegistryStatus(createResult);

		return FromRegistryStatus(RegSetValueExW(
			key.Get(),
			entry.ValueName,
			0,
			REG_SZ,
			reinterpret_cast<const BYTE*>(entry.Data),
			static_cast<DWORD>(characterCount * sizeof(wchar_t))));
	}

	HRESULT SetEntries(const std::span<const Entry> entries) noexcept
	{
		for (const auto& entry : entries)
			if (const HRESULT result = SetEntry(entry); FAILED(result))
				return result;
		return S_OK;
	}

	HRESULT DeleteRegistryPaths(const std::span<const PCWSTR> paths) noexcept
	{
		for (const PCWSTR path : paths)
		{
			if (path == nullptr)
				return E_INVALIDARG;
			const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, path);
			if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND &&
				status != ERROR_PATH_NOT_FOUND)
				return FromRegistryStatus(status);
		}
		return S_OK;
	}
}
