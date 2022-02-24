#include "pch.h"
#include "GlobalMem.h"

namespace Utility
{
	GlobalMem::~GlobalMem()
	{
		if (MemHandle == nullptr)
			return;
		if (MemPtr != nullptr)
			GlobalUnlock(MemHandle);
		GlobalFree(MemHandle);
	}

	HRESULT GlobalMem::AllocateAndLock(const size_t size)
	{
		const auto result = Allocate(size);
		return SUCCEEDED(result)
			? Lock()
			: result;
	}

	HRESULT GlobalMem::Allocate(const size_t size)
	{
		if (MemHandle != nullptr)
			return E_INVALIDARG;
		return (MemHandle = GlobalAlloc(GMEM_MOVEABLE | GMEM_NODISCARD, size)) != nullptr
			? S_OK
			: HRESULT_FROM_WIN32(GetLastError());
	}

	HRESULT GlobalMem::Lock()
	{
		return MemPtr != nullptr || (MemPtr = GlobalLock(MemHandle)) != nullptr
			? S_OK
			: HRESULT_FROM_WIN32(GetLastError());
	}

	HRESULT GlobalMem::Unlock()
	{
		if (MemPtr == nullptr)
			return S_OK;

		static_cast<void>(GlobalUnlock(MemHandle));		
		if (const auto result = GetLastError(); result != NO_ERROR)
			return HRESULT_FROM_WIN32(result);
		MemPtr = nullptr;
		return S_OK;
	}
}