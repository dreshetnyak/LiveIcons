#pragma once
#include "mobi.h"
#include "ParserBase.h"

namespace Parser
{
	class Mobi final : public Base
	{
		static HRESULT GetCoverImage(FILE* mobiFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		static HRESULT GetCoverImageFromContent(const char* image, const size_t size, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		static HRESULT GetCoverImageFileContent(const MOBIData* mobiData, char** outImage, size_t* outImageSize);
		static HRESULT GetCoverFromFirstReference(const MOBIData* mobiData, char** outImage, size_t* outImageSize);
		static HRESULT GetImageFromRecord(const MOBIData* mobiData, const size_t recordNumber, char** outImage, size_t* outImageSize);
	public:
		bool CanParse(const wstring& fileExtension) override;
		Result Parse(const wstring& fileName) override;
		Result Parse(IStream* stream) override;
	};
}
