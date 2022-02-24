#pragma once
#include "GlobalMem.h"

namespace Utility
{
	using namespace std;

	class DataIStream
	{
		HRESULT Result{S_OK};
		GlobalMem Memory{};
		IStream* Stream{};

	public:
		explicit DataIStream(const vector<char>& data);
		~DataIStream();

		[[nodiscard]] HRESULT GetHResult() const { return Result; }
		[[nodiscard]] IStream* GetIStream() const { return Stream; }
	};
}