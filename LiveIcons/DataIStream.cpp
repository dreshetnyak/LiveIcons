#include "pch.h"
#include "DataIStream.h"

namespace Utility
{
	DataIStream::DataIStream(const vector<char>& data)
	{
		const auto size = data.size();
				
		if (Result = Memory.AllocateAndLock(size); FAILED(Result))
			return;
		CopyMemory(Memory.Ptr(), data.data(), size);
		Memory.Unlock();

		Result = CreateStreamOnHGlobal(Memory.Handle(), false, &Stream);
	}

	DataIStream::~DataIStream()
	{
		if (Stream != nullptr)
			Stream->Release();
	}
}
