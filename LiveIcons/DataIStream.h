#pragma once
#include "ComPtr.h"
#include "GlobalMem.h"

namespace Utility
{
	using namespace std;

	class DataIStream final
	{
		HRESULT Result{S_OK};
		GlobalMem Memory{};
		ComPtr<IStream> Stream;

	public:
		explicit DataIStream(const char* data, size_t size);
		~DataIStream() noexcept = default;

		[[nodiscard]] HRESULT GetHResult() const noexcept { return Result; }
		[[nodiscard]] IStream* GetIStream() const noexcept { return Stream.Get(); }
	};
}
