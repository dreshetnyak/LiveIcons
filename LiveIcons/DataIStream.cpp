#include "pch.h"
#include "DataIStream.h"

namespace Utility
{
	DataIStream::DataIStream(const char* data, const size_t size)
	{
		if (Result = Memory.AllocateAndLock(size); FAILED(Result))
			return;
		CopyMemory(Memory.Ptr(), data, size);
		Memory.Unlock();

		Result = CreateStreamOnHGlobal(Memory.Handle(), false, &Stream);
	}

	DataIStream::~DataIStream()
	{
		if (Stream != nullptr)
			Stream->Release();
	}
}
