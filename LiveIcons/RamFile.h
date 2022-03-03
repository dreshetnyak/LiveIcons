#pragma once

namespace Utility
{
	using namespace std;

	class RamFile final
	{
		HRESULT HResult;
		FILE* RuntimeFileHandle;

		HRESULT CreateFileHandleFromStream(IStream* stream);
		HRESULT CreateRuntimeFileHandle(HANDLE fileHandle);

	public:
		explicit RamFile(IStream* stream);
		RamFile(const RamFile& other) = delete;
		RamFile(RamFile&& other) = delete;
		RamFile& operator=(const RamFile& other) = delete;
		RamFile& operator=(RamFile&& other) = delete;
		~RamFile();

		[[nodiscard]] HRESULT GetHResult() const;
		[[nodiscard]] FILE* GetFileHandle() const;
	};
}