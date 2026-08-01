#pragma once
#include <cstdint>

#include "zip.h"

namespace Zip
{
	using namespace std;
	
	class Position
	{
		unz64_file_pos FilePosition{};

		friend class Cache;

	public:
		const string FilePath;
		const size_t FileIndex;			// A separate index must be used instead of num_file_ because sometimes num_file_ goes out of order.
		const std::uint64_t UncompressedSize;

		explicit Position(const unz64_file_pos filePosition, string filePath, const size_t fileIndex, const std::uint64_t uncompressedSize) :
			FilePosition(filePosition),
			FilePath(std::move(filePath)),
			FileIndex(fileIndex),
			UncompressedSize(uncompressedSize)
		{ }
	};
}
