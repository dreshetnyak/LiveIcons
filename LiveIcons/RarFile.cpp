#include "pch.h"
#include "RarFile.h"

namespace Rar
{
	StreamFile::StreamFile()
	{
		//hFile = FILE_BAD_HANDLE;
		*FileName = 0;
		//NewFile = false;
		//LastWrite = false;
		//HandleType = FILE_HANDLENORMAL;
		//LineInput = false;
		//SkipClose = false;
		ErrorType = FILE_SUCCESS;
		OpenShared = false;
		//AllowDelete = true;
		//AllowExceptions = true;
		//PreserveAtime = false;
		//ReadErrorMode = FREM_ASK;
		//TruncatedAfterReadError = false;
		//CurFilePos = 0;
	}

	StreamFile::~StreamFile() = default;

	bool StreamFile::Open(const wchar* name, uint mode)
	{
		return true;
	}

	bool StreamFile::Close()
	{
		return true;
	}

	int StreamFile::Read(void* data, size_t size)
	{
		return File::Read(data, size);
	}

	void StreamFile::Seek(int64 offset, int method)
	{
		File::Seek(offset, method);
	}

	int64 StreamFile::Tell()
	{
		return File::Tell();
	}

	bool StreamFile::IsOpened()
	{
		return File::IsOpened();
	}
}

