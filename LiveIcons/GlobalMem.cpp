#include "pch.h"
#include "GlobalMem.h"

namespace Utility
{
	GlobalMem::GlobalMem(const size_t size) : MemSize(size), MemHandle()
	{
		if (!Allocate() || !Lock())
			ErrorMessage = GetLastErrorStr();
	}

	GlobalMem::~GlobalMem()
	{
		if (MemHandle == nullptr)
			return;
		if (MemPtr != nullptr)
			GlobalUnlock(MemHandle);
		GlobalFree(MemHandle);
	}

	bool GlobalMem::Allocate()
	{
		if (MemHandle != nullptr)
			return false;
		MemHandle = GlobalAlloc(GMEM_MOVEABLE | GMEM_NODISCARD, MemSize);
		return MemHandle != nullptr;
	}

	bool GlobalMem::Lock()
	{
		if (MemPtr != nullptr)
			return true;
		MemPtr = GlobalLock(MemHandle);
		return MemPtr != nullptr;
	}

	void GlobalMem::Unlock()
	{
		if (MemPtr == nullptr)
			return;
		GlobalUnlock(MemHandle);
		MemPtr = nullptr;
	}
}