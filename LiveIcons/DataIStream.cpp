#include "pch.h"
#include "DataIStream.h"

namespace Utility
{
	DataIStream::DataIStream(const char* data, const size_t size)
	{
		if (data == nullptr || size == 0)
		{
			Result = E_INVALIDARG;
			return;
		}
		if (Result = Memory.AllocateAndLock(size); FAILED(Result))
			return;
		CopyMemory(Memory.Ptr(), data, size);
		if (Result = Memory.Unlock(); FAILED(Result))
			return;

		Result = CreateStreamOnHGlobal(Memory.Handle(), false, Stream.Put());
	}
}
