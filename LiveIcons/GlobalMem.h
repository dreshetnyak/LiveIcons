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
		~GlobalMem() noexcept;

		HRESULT AllocateAndLock(size_t size) noexcept;
		HRESULT Allocate(size_t size) noexcept;
		HRESULT Lock() noexcept;
		HRESULT Unlock() noexcept;

		[[nodiscard]] HGLOBAL Handle() const noexcept { return MemHandle; }
		[[nodiscard]] void* Ptr() const noexcept { return MemPtr; }
		[[nodiscard]] bool Valid() const noexcept { return MemPtr != nullptr; }
	};
}
