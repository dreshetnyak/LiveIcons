#include "pch.h"
#include "ZipCache.h"

namespace
{
	constexpr ZPOS64_T MaximumArchiveEntries{ 10000U };
	constexpr uLong MaximumArchivePathBytes{ 4096U };
	constexpr size_t MaximumCachedPathBytes{ 8U * 1024U * 1024U };
}

namespace Zip
{
	Cache::Cache(const unzFile zipFile) : UnzFile{ zipFile }
	{
		unz_global_info64 archiveInfo{};
		ArchiveWithinLimits = UnzFile != nullptr &&
			unzGetGlobalInfo64(UnzFile, &archiveInfo) == UNZ_OK &&
			archiveInfo.number_entry <= MaximumArchiveEntries;
	}

	const Position* Cache::First()
	{
		if (!ArchiveWithinLimits)
			return END_OF_LIST;

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
		if (!ArchiveWithinLimits)
			return END_OF_LIST;

		if (CurrentPositionIndex == UNKNOWN)
			return First();

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
		if (!ArchiveWithinLimits || index >= MaximumArchiveEntries)
			return END_OF_LIST;

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
		CurrentPositionIndex = ArchiveWithinLimits && position.FileIndex < PositionsCache.size() &&
			unzGoToFilePos64(UnzFile, &position.FilePosition) == UNZ_OK
			? position.FileIndex
			: UNKNOWN;
	}
	
	// Private members

	const Position* Cache::CacheCurrent()
	{
		if (!ArchiveWithinLimits || PositionsCache.size() >= MaximumArchiveEntries)
			return END_OF_LIST;

		unz64_file_pos filePosition{};
		if (unzGetFilePos64(UnzFile, &filePosition) != UNZ_OK)
			return END_OF_LIST;

		string currentFilePath;
		unz_file_info64 currentFileInfo{};
		if (!ReadCurrentFileInfo(currentFilePath, currentFileInfo))
			return END_OF_LIST;
		const auto positionsCacheSize = PositionsCache.size();
		PositionsCache.emplace_back(filePosition, currentFilePath, positionsCacheSize, currentFileInfo.uncompressed_size);
		CachedPathBytes += currentFilePath.size();
		return &PositionsCache[positionsCacheSize];
	}

	bool Cache::ReadCurrentFileInfo(string& outFilePath, unz_file_info64& outFileInfo) const
	{
		if (unzGetCurrentFileInfo64(UnzFile, &outFileInfo, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK)
			return false;
		if (CachedPathBytes > MaximumCachedPathBytes ||
			outFileInfo.size_filename > MaximumArchivePathBytes ||
			outFileInfo.size_filename > MaximumCachedPathBytes - CachedPathBytes)
			return false;

		auto filePath = string{};
		filePath.resize(outFileInfo.size_filename);
		if (unzGetCurrentFileInfo64(UnzFile, nullptr, filePath.data(), static_cast<uLong>(filePath.size()), nullptr, 0, nullptr, 0) != UNZ_OK)
			return false;

		outFilePath = filePath;
		return true;
	}
}
