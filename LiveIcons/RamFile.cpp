#include "pch.h"
#include "RamFile.h"
#include "Utility.h"

namespace Utility
{
	// ReSharper disable once CppPossiblyUninitializedMember
	RamFile::RamFile(IStream* stream) : HResult(CreateFileHandleFromStream(stream)), RuntimeFileHandle(nullptr)
	{ }

	RamFile::~RamFile()
	{
        if (SUCCEEDED(HResult))
            fclose(RuntimeFileHandle);
	}

	HRESULT RamFile::GetHResult() const	{ return HResult; }

	FILE* RamFile::GetFileHandle() const { return RuntimeFileHandle; }

	HRESULT RamFile::CreateFileHandleFromStream(IStream* stream)
    {
        ULARGE_INTEGER fileSize{};
        if (const auto result = GetIStreamFileSize(stream, fileSize.QuadPart); FAILED(result))
	        return result;

        wstring fileName{};
        if (const auto result = GetTempFileFullName(fileName); FAILED(result))
	        return result;

        HANDLE fileHandle;
        if ((fileHandle = CreateFile(fileName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr)) == INVALID_HANDLE_VALUE)
	        return HRESULT_FROM_WIN32(GetLastError());
        
        if (const auto result = ReadIStream(stream, fileHandle); FAILED(result))
        {
            CloseHandle(fileHandle);
            return result;
        }        

        return CreateRuntimeFileHandle(fileHandle);
    }

    HRESULT RamFile::CreateRuntimeFileHandle(HANDLE fileHandle)
	{
        const auto runtimeFileDescriptor = _open_osfhandle(reinterpret_cast<intptr_t>(fileHandle), _O_RDWR);
        if (runtimeFileDescriptor == -1)
        {
            CloseHandle(fileHandle);
            return E_FAIL;
        }

        if ((RuntimeFileHandle = _fdopen(runtimeFileDescriptor, "r+")) == nullptr)
        {
            _close(runtimeFileDescriptor); // Also calls CloseHandle
            return E_FAIL;
        }

        return S_OK;
	}
}