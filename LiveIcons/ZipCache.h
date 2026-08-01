#pragma once
#include <deque>

#include "Zip.h"
#include "ZipPosition.h"

namespace Zip
{
	class Cache
	{
		unzFile UnzFile;
		// Positions are exposed by pointer. A deque keeps those pointers and
		// references valid while later archive entries are appended to the cache.
		deque<Position> PositionsCache{};
		size_t CurrentPositionIndex = UNKNOWN;
		size_t CachedPathBytes{};
		bool ArchiveWithinLimits{};

	public:
		explicit Cache(unzFile zipFile);

		[[nodiscard]] const Position* First();
		[[nodiscard]] const Position* Next();
		[[nodiscard]] const Position* At(size_t index);
		[[nodiscard]] const Position* Current() const
		{
			return ArchiveWithinLimits && CurrentPositionIndex < PositionsCache.size()
				? &PositionsCache[CurrentPositionIndex]
				: END_OF_LIST;
		}
		void SetCurrent(const Position& position);

	private:
		const Position* CacheCurrent();
		[[nodiscard]] bool ReadCurrentFileInfo(string& outFilePath, unz_file_info64& outFileInfo) const;
	};
}
