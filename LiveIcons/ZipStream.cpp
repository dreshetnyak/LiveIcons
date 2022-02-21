#include "pch.h"
#include "ZipStream.h"

namespace ZipStream
{
	voidpf CastFileNamePtrToIStream(voidpf opaque, const void* filename, int mode)
	{
		return new StreamInfo{ static_cast<IStream*>(const_cast<void*>(filename)), S_OK };
	}

	uLong Read(voidpf opaque, voidpf stream, void* buf, uLong size)
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		ULONG bytesRead;
		streamInfo->LastResult = streamInfo->FileStream->Read(buf, size, &bytesRead);
		return SUCCEEDED(streamInfo->LastResult)
			? bytesRead
			: 0;
	}

	uLong Write(voidpf opaque, voidpf stream, const void* buf, uLong size)
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		ULONG bytesWritten;
		streamInfo->LastResult = streamInfo->FileStream->Write(buf, size, &bytesWritten);
		return SUCCEEDED(streamInfo->LastResult)
			? bytesWritten
			: 0;
	}

	ZPOS64_T Tell(voidpf opaque, voidpf stream)
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		ULARGE_INTEGER currentPosition{};
		streamInfo->LastResult = streamInfo->FileStream->Seek(LARGE_INTEGER{ {0, 0} }, STREAM_SEEK_CUR, &currentPosition);
		return SUCCEEDED(streamInfo->LastResult)
			? currentPosition.QuadPart
			: static_cast<ZPOS64_T>(-1);
	}

	long Seek(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin)
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		ULARGE_INTEGER currentPosition{};
		LARGE_INTEGER newPosition;
		newPosition.QuadPart = static_cast<LONGLONG>(offset);
		const auto result = streamInfo->FileStream->Seek(newPosition, origin, &currentPosition);
		return SUCCEEDED(result) ? 0 : -1;
	}

	int Close(voidpf opaque, voidpf stream)
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		delete streamInfo;
		return 0;
	}

	int Error(voidpf opaque, voidpf stream)
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		return streamInfo != nullptr && streamInfo->LastResult == S_OK ? 0 : -1;
	}

	void SetIStreamHandlers(zlib_filefunc64_def* handlers)
	{
		handlers->zopen64_file = CastFileNamePtrToIStream;
		handlers->zread_file = Read;
		handlers->zwrite_file = Write;
		handlers->ztell64_file = Tell;
		handlers->zseek64_file = Seek;
		handlers->zclose_file = Close;
		handlers->zerror_file = Error;
		handlers->opaque = nullptr;
	}
}
