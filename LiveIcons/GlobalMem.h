#pragma once
#include <Windows.h>

namespace Utility
{
	class GlobalMem final
	{
		HGLOBAL MemHandle{};
		void* MemPtr{};

	public:
		GlobalMem() = default;
		GlobalMem(const GlobalMem&) = delete;
		GlobalMem& operator=(const GlobalMem&) = delete;
		~GlobalMem();

		HRESULT AllocateAndLock(size_t size);
		HRESULT Allocate(size_t size);
		HRESULT Lock();
		HRESULT Unlock();

		[[nodiscard]] HGLOBAL Handle() const { return MemHandle; }
		[[nodiscard]] void* Ptr() const { return MemPtr; }
		[[nodiscard]] bool Valid() const { return MemPtr != nullptr; }
	};
}