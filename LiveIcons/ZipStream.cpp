#include "pch.h"
#include "ZipStream.h"

namespace ZipStream
{
	voidpf CastFileNamePtrToIStream(voidpf, const void* filename, int) noexcept
	{
		try
		{
			auto* const fileStream = static_cast<IStream*>(const_cast<void*>(filename));
			if (fileStream == nullptr)
				return nullptr;
			auto streamInfo = std::make_unique<StreamInfo>();
			streamInfo->FileStream = fileStream;
			streamInfo->LastResult = S_OK;
			return streamInfo.release();
		}
		catch (...)
		{
			return nullptr;
		}
	}

	uLong Read(voidpf, voidpf stream, void* buf, const uLong size) noexcept
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		if (streamInfo == nullptr)
			return 0;
		if (streamInfo->FileStream == nullptr || (buf == nullptr && size != 0))
		{
			streamInfo->LastResult = E_POINTER;
			return 0;
		}

		try
		{
			ULONG bytesRead{};
			streamInfo->LastResult = streamInfo->FileStream->Read(buf, size, &bytesRead);
			if (FAILED(streamInfo->LastResult))
				return 0;
			if (bytesRead > size)
			{
				streamInfo->LastResult = STG_E_READFAULT;
				return 0;
			}
			return bytesRead;
		}
		catch (...)
		{
			streamInfo->LastResult = E_UNEXPECTED;
			return 0;
		}
	}

	uLong Write(voidpf, voidpf stream, const void* buf, const uLong size) noexcept
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		if (streamInfo == nullptr)
			return 0;
		if (streamInfo->FileStream == nullptr || (buf == nullptr && size != 0))
		{
			streamInfo->LastResult = E_POINTER;
			return 0;
		}

		try
		{
			ULONG bytesWritten{};
			streamInfo->LastResult = streamInfo->FileStream->Write(buf, size, &bytesWritten);
			if (FAILED(streamInfo->LastResult))
				return 0;
			if (bytesWritten > size)
			{
				streamInfo->LastResult = STG_E_WRITEFAULT;
				return 0;
			}
			return bytesWritten;
		}
		catch (...)
		{
			streamInfo->LastResult = E_UNEXPECTED;
			return 0;
		}
	}

	ZPOS64_T Tell(voidpf, voidpf stream) noexcept
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		if (streamInfo == nullptr)
			return static_cast<ZPOS64_T>(-1);
		if (streamInfo->FileStream == nullptr)
		{
			streamInfo->LastResult = E_POINTER;
			return static_cast<ZPOS64_T>(-1);
		}

		try
		{
			ULARGE_INTEGER currentPosition{};
			streamInfo->LastResult = streamInfo->FileStream->Seek(
				LARGE_INTEGER{ {0, 0} }, STREAM_SEEK_CUR, &currentPosition);
			return SUCCEEDED(streamInfo->LastResult)
				? currentPosition.QuadPart
				: static_cast<ZPOS64_T>(-1);
		}
		catch (...)
		{
			streamInfo->LastResult = E_UNEXPECTED;
			return static_cast<ZPOS64_T>(-1);
		}
	}

	long Seek(voidpf, voidpf stream, const ZPOS64_T offset, const int origin) noexcept
	{
		const auto streamInfo = static_cast<StreamInfo*>(stream);
		if (streamInfo == nullptr)
			return -1;
		if (streamInfo->FileStream == nullptr)
		{
			streamInfo->LastResult = E_POINTER;
			return -1;
		}
		if (origin < STREAM_SEEK_SET || origin > STREAM_SEEK_END)
		{
			streamInfo->LastResult = E_INVALIDARG;
			return -1;
		}
		if (offset > static_cast<ZPOS64_T>((std::numeric_limits<LONGLONG>::max)()))
		{
			streamInfo->LastResult = STG_E_INVALIDFUNCTION;
			return -1;
		}

		try
		{
			ULARGE_INTEGER currentPosition{};
			LARGE_INTEGER newPosition{};
			newPosition.QuadPart = static_cast<LONGLONG>(offset);
			streamInfo->LastResult = streamInfo->FileStream->Seek(
				newPosition, origin, &currentPosition);
			return SUCCEEDED(streamInfo->LastResult) ? 0 : -1;
		}
		catch (...)
		{
			streamInfo->LastResult = E_UNEXPECTED;
			return -1;
		}
	}

	int Close(voidpf, voidpf stream) noexcept
	{
		try
		{
			delete static_cast<StreamInfo*>(stream);
		}
		catch (...)
		{
			return -1;
		}
		return 0;
	}

	int Error(voidpf, voidpf stream) noexcept
	{
		try
		{
			const auto streamInfo = static_cast<StreamInfo*>(stream);
			return streamInfo != nullptr && SUCCEEDED(streamInfo->LastResult) ? 0 : -1;
		}
		catch (...)
		{
			return -1;
		}
	}

	void SetIStreamHandlers(zlib_filefunc64_def* handlers) noexcept
	{
		if (handlers == nullptr)
			return;
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
