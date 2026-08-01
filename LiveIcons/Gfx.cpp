#include "pch.h"
#include "Gfx.h"
#include "DataIStream.h"

#include <atlbase.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace Gfx
{
	namespace
	{
		constexpr std::uint64_t BytesPerPixel{ 4 };
		constexpr std::uint64_t MaximumEncodedImageBytes{ 64ULL * 1024ULL * 1024ULL };
		constexpr std::uint64_t MaximumDecodedBitmapBytes{ 64ULL * 1024ULL * 1024ULL };

		struct BitmapDeleter final
		{
			void operator()(HBITMAP bitmap) const noexcept
			{
				if (bitmap != nullptr)
					DeleteObject(bitmap);
			}
		};

		using UniqueBitmap = std::unique_ptr<std::remove_pointer_t<HBITMAP>, BitmapDeleter>;

		HRESULT ConvertPixelFormat(
			IWICBitmapSource* bitmapSource,
			IWICImagingFactory* imagingFactory,
			CComPtr<IWICBitmapSource>& outConvertedSource) noexcept
		{
			outConvertedSource.Release();
			if (bitmapSource == nullptr || imagingFactory == nullptr)
				return E_POINTER;

			CComPtr<IWICFormatConverter> formatConverter;
			HRESULT result = imagingFactory->CreateFormatConverter(&formatConverter);
			if (FAILED(result))
				return result;

			result = formatConverter->Initialize(
				bitmapSource,
				GUID_WICPixelFormat32bppBGRA,
				WICBitmapDitherTypeNone,
				nullptr,
				0,
				WICBitmapPaletteTypeCustom);
			if (FAILED(result))
				return result;

			return formatConverter->QueryInterface(&outConvertedSource);
		}

		HRESULT Get32BppSource(
			IWICBitmapSource* bitmapSource,
			IWICImagingFactory* imagingFactory,
			CComPtr<IWICBitmapSource>& outConvertedSource) noexcept
		{
			outConvertedSource.Release();
			if (bitmapSource == nullptr || imagingFactory == nullptr)
				return E_POINTER;

			WICPixelFormatGUID sourcePixelFormat{};
			const HRESULT result = bitmapSource->GetPixelFormat(&sourcePixelFormat);
			if (FAILED(result))
				return result;

			return sourcePixelFormat == GUID_WICPixelFormat32bppBGRA
				? bitmapSource->QueryInterface(&outConvertedSource)
				: ConvertPixelFormat(bitmapSource, imagingFactory, outConvertedSource);
		}

		HRESULT ConvertBitmapSourceTo32BppHBitmap(
			IWICBitmapSource* bitmapSource,
			IWICImagingFactory* imagingFactory,
			HBITMAP& outConvertedBitmap,
			SIZE& outImageSize) noexcept
		{
			outConvertedBitmap = nullptr;
			outImageSize = {};
			if (bitmapSource == nullptr || imagingFactory == nullptr)
				return E_POINTER;

			CComPtr<IWICBitmapSource> convertedSource;
			HRESULT result = Get32BppSource(bitmapSource, imagingFactory, convertedSource);
			if (FAILED(result))
				return result;

			UINT sourceWidth = 0;
			UINT sourceHeight = 0;
			result = convertedSource->GetSize(&sourceWidth, &sourceHeight);
			if (FAILED(result))
				return result;

			const auto width = static_cast<std::uint64_t>(sourceWidth);
			const auto height = static_cast<std::uint64_t>(sourceHeight);
			const auto maximumLong = static_cast<std::uint64_t>((std::numeric_limits<LONG>::max)());
			const auto maximumPixels = MaximumDecodedBitmapBytes / BytesPerPixel;
			if (width == 0 || height == 0)
				return WINCODEC_ERR_BADIMAGE;
			if (width > maximumLong || height > maximumLong || width > maximumPixels / height)
			{
				return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
			}

			const auto stride = width * BytesPerPixel;
			const auto bufferSize = stride * height;
			if (stride > (std::numeric_limits<UINT>::max)() ||
				bufferSize > (std::numeric_limits<UINT>::max)())
			{
				return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
			}

			BITMAPINFO bitmapInfo{};
			bitmapInfo.bmiHeader.biSize = sizeof bitmapInfo.bmiHeader;
			bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(sourceWidth);
			bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(sourceHeight);
			bitmapInfo.bmiHeader.biPlanes = 1;
			bitmapInfo.bmiHeader.biBitCount = 32;
			bitmapInfo.bmiHeader.biCompression = BI_RGB;

			BYTE* bitmapBits = nullptr;
			SetLastError(ERROR_SUCCESS);
			UniqueBitmap bitmap{ CreateDIBSection(
				nullptr,
				&bitmapInfo,
				DIB_RGB_COLORS,
				reinterpret_cast<void**>(&bitmapBits),
				nullptr,
				0) };
			if (!bitmap)
			{
				const DWORD error = GetLastError();
				return error == ERROR_SUCCESS ? E_OUTOFMEMORY : HRESULT_FROM_WIN32(error);
			}
			if (bitmapBits == nullptr)
				return E_UNEXPECTED;

			const WICRect sourceRectangle{
				0,
				0,
				static_cast<INT>(sourceWidth),
				static_cast<INT>(sourceHeight) };
			result = convertedSource->CopyPixels(
				&sourceRectangle,
				static_cast<UINT>(stride),
				static_cast<UINT>(bufferSize),
				bitmapBits);
			if (FAILED(result))
				return result;

			outImageSize = {
				static_cast<LONG>(sourceWidth),
				static_cast<LONG>(sourceHeight) };
			outConvertedBitmap = bitmap.release();
			return S_OK;
		}

		HRESULT WicCreate32BitsPerPixelHBitmap(
			IStream* stream,
			HBITMAP& outNewBitmap,
			WTS_ALPHATYPE& outAlphaType,
			SIZE& outImageSize) noexcept
		{
			outNewBitmap = nullptr;
			outAlphaType = WTSAT_UNKNOWN;
			outImageSize = {};
			if (stream == nullptr)
				return E_POINTER;

			CComPtr<IWICImagingFactory> imagingFactory;
			HRESULT result = CoCreateInstance(
				CLSID_WICImagingFactory,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&imagingFactory));
			if (FAILED(result))
				return result;

			CComPtr<IWICBitmapDecoder> decoder;
			result = imagingFactory->CreateDecoderFromStream(
				stream,
				&GUID_VendorMicrosoft,
				WICDecodeMetadataCacheOnDemand,
				&decoder);
			if (FAILED(result))
				return result;

			CComPtr<IWICBitmapFrameDecode> bitmapFrame;
			result = decoder->GetFrame(0, &bitmapFrame);
			if (FAILED(result))
				return result;

			result = ConvertBitmapSourceTo32BppHBitmap(
				bitmapFrame,
				imagingFactory,
				outNewBitmap,
				outImageSize);
			if (SUCCEEDED(result))
				outAlphaType = WTSAT_ARGB;

			return result;
		}
	}

	HRESULT LoadImageToHBitmap(
		const char* sourceImage,
		const std::size_t size,
		HBITMAP& outBitmap,
		WTS_ALPHATYPE& outAlphaType,
		SIZE& imageSize) noexcept
	{
		outBitmap = nullptr;
		outAlphaType = WTSAT_UNKNOWN;
		imageSize = {};

		if (sourceImage == nullptr)
			return E_POINTER;
		if (size == 0)
			return E_INVALIDARG;
		if (size > MaximumEncodedImageBytes)
			return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

		try
		{
			const Utility::DataIStream imageStream{ sourceImage, size };
			const HRESULT result = imageStream.GetHResult();
			return FAILED(result)
				? result
				: WicCreate32BitsPerPixelHBitmap(
					imageStream.GetIStream(), outBitmap, outAlphaType, imageSize);
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_FAIL;
		}
	}

	bool ImageSizeSatisfiesCoverConstraints(const SIZE& imageSize) noexcept
	{
		constexpr LONG MinimumCoverDimension = 20;
		const auto width = static_cast<std::int64_t>(imageSize.cx);
		const auto height = static_cast<std::int64_t>(imageSize.cy);
		return width > MinimumCoverDimension && height > MinimumCoverDimension &&
			(height >= width || width < height * 2);
	}
}
