#include "pch.h"
#include "GlobalMem.h"

namespace Utility
{
	GlobalMem::~GlobalMem() noexcept
	{
		if (MemHandle == nullptr)
			return;
		if (MemPtr != nullptr)
			GlobalUnlock(MemHandle);
		GlobalFree(MemHandle);
	}

	HRESULT GlobalMem::AllocateAndLock(const size_t size) noexcept
	{
		const auto result = Allocate(size);
		return SUCCEEDED(result)
			? Lock()
			: result;
	}

	HRESULT GlobalMem::Allocate(const size_t size) noexcept
	{
		if (MemHandle != nullptr)
			return E_INVALIDARG;
		if (size == 0)
			return E_INVALIDARG;
		return (MemHandle = GlobalAlloc(GMEM_MOVEABLE | GMEM_NODISCARD, size)) != nullptr
			? S_OK
			: E_OUTOFMEMORY;
	}

	HRESULT GlobalMem::Lock() noexcept
	{
		if (MemHandle == nullptr)
			return E_UNEXPECTED;
		SetLastError(NO_ERROR);
		if (MemPtr != nullptr || (MemPtr = GlobalLock(MemHandle)) != nullptr)
			return S_OK;
		const DWORD error = GetLastError();
		return HRESULT_FROM_WIN32(error != NO_ERROR ? error : ERROR_GEN_FAILURE);
	}

	HRESULT GlobalMem::Unlock() noexcept
	{
		if (MemPtr == nullptr)
			return S_OK;

		// GlobalUnlock returns zero both when the final lock is released (success)
		// and on failure. Clear last-error first to distinguish those cases.
		SetLastError(NO_ERROR);
		const BOOL remainsLocked = GlobalUnlock(MemHandle);
		const DWORD result = GetLastError();
		if (remainsLocked == FALSE && result != NO_ERROR)
			return HRESULT_FROM_WIN32(result);
		MemPtr = nullptr;
		return S_OK;
	}
}
