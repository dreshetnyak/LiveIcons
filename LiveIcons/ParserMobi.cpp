#include "pch.h"
#include "ParserMobi.h"
#include "Gfx.h"
#include "mobi.h"
#include "RamFile.h"
#include "Utility.h"
#include "XmlDocument.h"

namespace
{
	constexpr std::size_t MaximumMobiResourceBytes{ 64ULL * 1024ULL * 1024ULL };

	struct MobiDataDeleter final
	{
		void operator()(MOBIData* const data) const noexcept
		{
			if (data != nullptr)
				mobi_free(data);
		}
	};

	using UniqueMobiData = std::unique_ptr<MOBIData, MobiDataDeleter>;
	using UniqueFile = std::unique_ptr<FILE, decltype(&fclose)>;
}

namespace Parser
{
	bool Mobi::CanParse(const wstring& fileExtension)
	{
		return StrLib::EqualsCi(fileExtension, wstring{ L".mobi" }) || 
			StrLib::EqualsCi(fileExtension, wstring{ L".azw3" }) ||
			StrLib::EqualsCi(fileExtension, wstring{ L".azw" });
	}

	Result Mobi::Parse(const wstring& fileName)
	{
		_set_doserrno(0);
		UniqueFile file{ _wfsopen(fileName.c_str(), L"rb", _SH_DENYWR), &fclose };
		if (file == nullptr)
			return Result{ _doserrno != 0 ? HRESULT_FROM_WIN32(_doserrno) : E_FAIL };

		HBITMAP coverBitmap{ nullptr };
		WTS_ALPHATYPE coverBitmapAlpha{};
		const auto result = GetCoverImage(file.get(), coverBitmap, coverBitmapAlpha);
		return SUCCEEDED(result)
			? Result{ {}, coverBitmap, coverBitmapAlpha }
			: Result{ result };
	}

	Result Mobi::Parse(IStream* stream)
	{
		const Utility::RamFile file{ stream };
		if (const auto result = file.GetHResult(); FAILED(result))
			return Result{ result };

		HBITMAP coverBitmap{ nullptr };
		WTS_ALPHATYPE coverBitmapAlpha{};
		const auto result = GetCoverImage(file.GetFileHandle(), coverBitmap, coverBitmapAlpha);
		return SUCCEEDED(result)
			? Result{ {}, coverBitmap, coverBitmapAlpha }
			: Result{ result };
	}

	HRESULT Mobi::GetCoverImage(FILE* mobiFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
	{
		UniqueMobiData mobiData{ mobi_init() };
		if (mobiData == nullptr)
			return E_OUTOFMEMORY;

		// Loader selection must be configured before mobi_load_file parses and may
		// swap the two halves of a hybrid KF7/KF8 document.
		if (mobi_parse_kf7(mobiData.get()) != MOBI_SUCCESS)
			return E_FAIL;
		if (mobi_load_file(mobiData.get(), mobiFile) != MOBI_SUCCESS)
			return E_FAIL;

		char* image;
		size_t imageSize;
		HRESULT result;		

		if (SUCCEEDED(result = GetCoverImageFileContent(mobiData.get(), &image, &imageSize)) ||
			SUCCEEDED(result = GetCoverFromFirstReference(mobiData.get(), &image, &imageSize)))
			result = GetCoverImageFromContent(image, imageSize, outBitmap, outAlphaType);

		return result;
	}

	HRESULT Mobi::GetCoverImageFromContent(const char* image, const size_t size, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType)
	{
		SIZE imageSize{};
		if (const auto result = Gfx::LoadImageToHBitmap(image, size, outBitmap, outAlphaType, imageSize); FAILED(result))
			return result;

		if (Gfx::ImageSizeSatisfiesCoverConstraints(imageSize))
			return S_OK;

		DeleteObject(outBitmap);
		outBitmap = nullptr;
		outAlphaType = WTSAT_UNKNOWN;
		return E_FAIL;
	}

	HRESULT Mobi::GetCoverImageFileContent(const MOBIData* mobiData, char** outImage, size_t* outImageSize)
	{
		MOBIExthHeader* exthRecord = mobi_get_exthrecord_by_tag(mobiData, EXTH_COVEROFFSET);
		if (exthRecord == nullptr)
			return E_FAIL;

		const auto offset = mobi_decode_exthvalue(static_cast<const unsigned char*>(exthRecord->data), exthRecord->size);
		const auto firstResource = mobi_get_first_resource_record(mobiData);
		if (firstResource > (std::numeric_limits<size_t>::max)() - offset)
			return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
		const auto record = mobi_get_record_by_seqnumber(mobiData, firstResource + offset);
		if (record == nullptr || record->size < 4 ||
			record->size > MaximumMobiResourceBytes)
			return E_FAIL;

		*outImage = reinterpret_cast<char*>(record->data);
		*outImageSize = record->size;
		return S_OK;
	}

	HRESULT Mobi::GetCoverFromFirstReference(const MOBIData* mobiData, char** outImage, size_t* outImageSize)
	{
		if (mobiData->rh == nullptr)
			return E_FAIL;

		size_t rawDataSize = mobiData->rh->text_length;
		if (rawDataSize > MaximumMobiResourceBytes)
			return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
		const unique_ptr<char[]> rawData{ new (nothrow) char[rawDataSize + 1] };
		if (rawData == nullptr)
			return E_OUTOFMEMORY;

		if (mobi_get_rawml(mobiData, rawData.get(), &rawDataSize) != MOBI_SUCCESS)
			return E_FAIL;
		if (rawDataSize > MaximumMobiResourceBytes)
			return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

		const string file{ rawData.get(), rawDataSize };
		const Xml::Document xmlFile{ file };
		size_t recNumber{}, height{}, width{};
		string imgTag, recNumberStr, heightStr, widthStr;
		for (size_t offset{ 0 }, tagOffset{0}; xmlFile.GetTag("img", offset, tagOffset, imgTag); offset += imgTag.size())
		{
			offset = tagOffset;

			if (!Xml::Document::GetTagAttribute(imgTag, "recindex", recNumberStr) || 
				!Utility::TryParseNumber(recNumberStr, recNumber))
				continue;

			if (Xml::Document::GetTagAttribute(imgTag, "height", heightStr) && 
				Xml::Document::GetTagAttribute(imgTag, "width", widthStr) && 
				Utility::TryParseNumber(heightStr, height) &&
				Utility::TryParseNumber(widthStr, width) &&
				!Gfx::ImageSizeSatisfiesCoverConstraints(SIZE{static_cast<long>(width), static_cast<long>(height)}))
				continue;

			return GetImageFromRecord(mobiData, recNumber, outImage, outImageSize);
		}		

		return E_FAIL;
	}

	HRESULT Mobi::GetImageFromRecord(const MOBIData* mobiData, const size_t recordNumber, char** outImage, size_t* outImageSize)
	{
		const auto mobiHeader = mobiData->mh;
		if (mobiHeader == nullptr || mobiHeader->image_index == nullptr ||
			mobiHeader->last_text_index == nullptr)
			return E_FAIL;

		uint32_t index = 0;
		const auto firstImageIndex = *mobiHeader->image_index;
		const auto lastImageIndex = *mobiHeader->last_text_index;
		if (firstImageIndex == MOBI_NOTSET || lastImageIndex == UINT16_MAX)
			return E_FAIL;

		for (auto record = mobiData->rec; record != nullptr && index <= lastImageIndex; record = record->next, ++index)
		{
			if (index < firstImageIndex || index - firstImageIndex + 1 != recordNumber)
				continue;

			if (record->size > MaximumMobiResourceBytes)
				return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
			*outImage = reinterpret_cast<char*>(record->data);
			*outImageSize = record->size;
			return S_OK;
		}

		return E_FAIL;
	}
}
