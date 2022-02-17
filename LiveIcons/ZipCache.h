#pragma once
#include "Zip.h"
#include "ZipPosition.h"

namespace Zip
{
	class Cache
	{
		unz64_s* UnzFile;
		vector<Position> PositionsCache{};
		size_t CurrentPositionIndex = UNKNOWN;

	public:
		explicit Cache(const unzFile zipFile) : UnzFile(static_cast<unz64_s*>(zipFile))
		{ }

		[[nodiscard]] const Position* First();
		[[nodiscard]] const Position* Next();
		[[nodiscard]] const Position* At(size_t index);
		[[nodiscard]] const Position* Current() const { return CurrentPositionIndex != UNKNOWN ? &PositionsCache[CurrentPositionIndex] : END_OF_LIST; }
		void SetCurrent(const Position& position);

	private:
		const Position* CacheCurrent();
		[[nodiscard]] bool ReadCurrentFilePath(string& outFilePath) const;
	};
}