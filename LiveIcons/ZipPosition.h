#pragma once
#include "zip.h"

namespace Zip
{
	using namespace std;
	
	class Position
	{
		// Members that correspond to the position in zip file
		ZPOS64_T NumFile;
		ZPOS64_T PosInCentralDir;
		unz_file_info64 CurFileInfo{};
		unz_file_info64_internal CurFileInfoInternal{};

		friend class Cache;

	public:
		const string FilePath;
		const size_t FileIndex;			// A separate index must be used instead of num_file_ because sometimes num_file_ goes out of order.
		const size_t UncompressedSize;

		explicit Position(const unzFile& zipFile, string filePath, const size_t fileIndex) :
			NumFile(static_cast<unz64_s*>(zipFile)->num_file),
			PosInCentralDir(static_cast<unz64_s*>(zipFile)->pos_in_central_dir),
			CurFileInfo(static_cast<unz64_s*>(zipFile)->cur_file_info),
			CurFileInfoInternal(static_cast<unz64_s*>(zipFile)->cur_file_info_internal),
			FilePath(std::move(filePath)),
			FileIndex(fileIndex),
			UncompressedSize(CurFileInfo.uncompressed_size)
		{ }
	};
}