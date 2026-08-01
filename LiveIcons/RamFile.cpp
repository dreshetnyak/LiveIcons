#include "pch.h"
#include "RamFile.h"
#include "Utility.h"

namespace
{
	class UniqueFileHandle final
	{
		HANDLE Handle{ INVALID_HANDLE_VALUE };

	public:
		explicit UniqueFileHandle(const HANDLE handle) noexcept : Handle{ handle } { }
		UniqueFileHandle(const UniqueFileHandle&) = delete;
		UniqueFileHandle& operator=(const UniqueFileHandle&) = delete;

		~UniqueFileHandle() noexcept
		{
			if (Handle != INVALID_HANDLE_VALUE)
				CloseHandle(Handle);
		}

		[[nodiscard]] HANDLE Get() const noexcept { return Handle; }

		[[nodiscard]] HANDLE Release() noexcept
		{
			const HANDLE handle = Handle;
			Handle = INVALID_HANDLE_VALUE;
			return handle;
		}
	};
}

namespace Utility
{
	RamFile::RamFile(IStream* stream) : HResult{ E_FAIL }, RuntimeFileHandle{ nullptr }
	{
		// Both members must be initialized before this helper stores the FILE*.
		// Calling it from HResult's member initializer allowed the later
		// RuntimeFileHandle initializer to overwrite the successfully opened file.
		HResult = CreateFileHandleFromStream(stream);
	}

	RamFile::~RamFile()
	{
		if (RuntimeFileHandle != nullptr)
			static_cast<void>(fclose(RuntimeFileHandle));
	}

	HRESULT RamFile::GetHResult() const	{ return HResult; }

	FILE* RamFile::GetFileHandle() const { return RuntimeFileHandle; }

	HRESULT RamFile::CreateFileHandleFromStream(IStream* stream)
    {
		if (stream == nullptr)
			return E_POINTER;

        wstring fileName{};
        if (const auto result = GetTempFileFullName(fileName); FAILED(result))
	        return result;

		UniqueFileHandle fileHandle{ CreateFile(fileName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr) };
		if (fileHandle.Get() == INVALID_HANDLE_VALUE)
		{
			const DWORD error = GetLastError();
			DeleteFileW(fileName.c_str());
			return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
		}
        
		if (const auto result = ReadIStream(stream, fileHandle.Get()); FAILED(result))
			return result;

		// _open_osfhandle consumes the Win32 handle on success; the CRT FILE then
		// owns it. CreateRuntimeFileHandle also closes it on every failure path.
		return CreateRuntimeFileHandle(fileHandle.Release());
	}

    HRESULT RamFile::CreateRuntimeFileHandle(HANDLE fileHandle)
	{
		const auto runtimeFileDescriptor = _open_osfhandle(
			reinterpret_cast<intptr_t>(fileHandle), _O_RDWR | _O_BINARY);
        if (runtimeFileDescriptor == -1)
        {
            CloseHandle(fileHandle);
            return E_FAIL;
        }

		if ((RuntimeFileHandle = _fdopen(runtimeFileDescriptor, "r+b")) == nullptr)
        {
            _close(runtimeFileDescriptor); // Also calls CloseHandle
            return E_FAIL;
        }

        return S_OK;
	}
}
