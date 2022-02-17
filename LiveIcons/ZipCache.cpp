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

		if (unzGoToFirstFile(UnzFile) != UNZ_OK || !UnzFile->current_file_ok)
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

		if (unzGoToNextFile(UnzFile) != UNZ_OK || !UnzFile->current_file_ok)
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
		UnzFile->num_file = position.NumFile;
		UnzFile->pos_in_central_dir = position.PosInCentralDir;
		UnzFile->cur_file_info = position.CurFileInfo;
		UnzFile->cur_file_info_internal = position.CurFileInfoInternal;
		CurrentPositionIndex = position.FileIndex;		
	}
	
	// Private members

	const Position* Cache::CacheCurrent()
	{
		if (!UnzFile->current_file_ok)
			return END_OF_LIST;
		string currentFilePath;
		if (!ReadCurrentFilePath(currentFilePath))
			return END_OF_LIST;
		const auto positionsCacheSize = PositionsCache.size();
		PositionsCache.emplace_back(UnzFile, currentFilePath, positionsCacheSize);
		return &PositionsCache[positionsCacheSize];
	}

	bool Cache::ReadCurrentFilePath(string& outFilePath) const
	{
		const auto file_path_size = UnzFile->cur_file_info.size_filename;
		auto filePath = string{};
		filePath.resize(file_path_size);
		if (unzGetCurrentFileInfo64(UnzFile, nullptr, filePath.data(), file_path_size, nullptr, 0, nullptr, 0) != UNZ_OK)
			return false;
		filePath.resize(file_path_size);
		outFilePath = filePath;
		return true;
	}
}