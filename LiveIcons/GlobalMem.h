#pragma once
#include <string>
#include <Windows.h>
#include <system_error>
#include "Utility.h"

namespace Utility
{
	class GlobalMem final
	{
		size_t MemSize;
		HGLOBAL MemHandle;
		void* MemPtr;
		wstring ErrorMessage;

	public:
		explicit GlobalMem(size_t size);
		GlobalMem(const GlobalMem&) = delete;
		GlobalMem& operator=(const GlobalMem&) = delete;
		~GlobalMem();

		bool Allocate();
		bool Lock();
		void Unlock();

		[[nodiscard]] HGLOBAL Handle() const { return MemHandle; }
		[[nodiscard]] void* Ptr() const { return MemPtr; }
		[[nodiscard]] size_t Size() const { return MemSize; }
		[[nodiscard]] wstring Error() { return ErrorMessage; }
		[[nodiscard]] bool Valid() const { return MemPtr != nullptr; }
	};
}