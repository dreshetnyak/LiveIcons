#include "pch.h"
#include "ZipCache.h"

namespace Zip
{
	const Position* Cache::First()
	{
		if (!PositionsCache.empty())
		{
			if (CurrentPositionIndex != 0)
				SetCurrent(PositionsCache[0]);
			return &PositionsCache[0];
		}

		if (unzGoToFirstFile(UnzFile) != UNZ_OK)
			return END_OF_LIST;
		CurrentPositionIndex = 0;
		return CacheCurrent();
	}
		
	const Position* Cache::Next()
	{
		const auto nextIndex = CurrentPositionIndex + 1;
		if (nextIndex < PositionsCache.size())
		{
			const Position& pos = PositionsCache[nextIndex];
			SetCurrent(pos);
			return &pos;
		}

		if (unzGoToNextFile(UnzFile) != UNZ_OK)
			return END_OF_LIST;		
		CurrentPositionIndex = nextIndex;
		return CacheCurrent();
	}

	const Position* Cache::At(const size_t index)
	{
		const Position* pos;
		const auto positionsCacheSize = PositionsCache.size();
		if (index < positionsCacheSize)
		{
			pos = &PositionsCache[index];
			SetCurrent(*pos);
			return pos;
		}

		if (positionsCacheSize != 0)
		{
			pos = &PositionsCache[positionsCacheSize - 1];
			SetCurrent(*pos);
		}
		else
			pos = First();

		for (; pos != END_OF_LIST; pos = Next())
		{
			if (pos->FileIndex == index)
				return pos;			
		}

		return END_OF_LIST;
	}

	void Cache::SetCurrent(const Position& position)
	{
		CurrentPositionIndex = unzGoToFilePos64(UnzFile, &position.FilePosition) == UNZ_OK
			? position.FileIndex
			: UNKNOWN;
	}
	
	// Private members

	const Position* Cache::CacheCurrent()
	{
		unz64_file_pos filePosition{};
		if (unzGetFilePos64(UnzFile, &filePosition) != UNZ_OK)
			return END_OF_LIST;

		string currentFilePath;
		unz_file_info64 currentFileInfo{};
		if (!ReadCurrentFileInfo(currentFilePath, currentFileInfo))
			return END_OF_LIST;
		const auto positionsCacheSize = PositionsCache.size();
		PositionsCache.emplace_back(filePosition, currentFilePath, positionsCacheSize, static_cast<size_t>(currentFileInfo.uncompressed_size));
		return &PositionsCache[positionsCacheSize];
	}

	bool Cache::ReadCurrentFileInfo(string& outFilePath, unz_file_info64& outFileInfo) const
	{
		if (unzGetCurrentFileInfo64(UnzFile, &outFileInfo, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK)
			return false;

		auto filePath = string{};
		filePath.resize(outFileInfo.size_filename);
		if (unzGetCurrentFileInfo64(UnzFile, nullptr, filePath.data(), static_cast<uLong>(filePath.size()), nullptr, 0, nullptr, 0) != UNZ_OK)
			return false;

		outFilePath = filePath;
		return true;
	}
}
