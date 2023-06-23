#include "pch.h"
#include "Gfx.h"
#include "DataIStream.h"
#include "Utility.h"

namespace Gfx
{
	HRESULT ConvertPixelFormat(IWICBitmapSource* bitmapSource, IWICImagingFactory* imagingFactory, IWICBitmapSource** bitmapSourceConverted)
	{
		IWICFormatConverter* formatConverter;
		if (const auto result = imagingFactory->CreateFormatConverter(&formatConverter); FAILED(result))
			return result;

		// Create the appropriate pixel format converter
		auto result = formatConverter->Initialize(bitmapSource, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
		if (SUCCEEDED(result))
			result = formatConverter->QueryInterface(bitmapSourceConverted);

		formatConverter->Release();
		return result;
	}

	HRESULT ConvertBitmapSourceTo32BppHBitmap(IWICBitmapSource* bitmapSource, IWICImagingFactory* imagingFactory, HBITMAP& outConvertedBitmap, SIZE& imageSize)
	{
		outConvertedBitmap = nullptr;

		IWICBitmapSource* bitmapSourceConverted = nullptr;
		WICPixelFormatGUID guidPixelFormatSource;

		auto result = bitmapSource->GetPixelFormat(&guidPixelFormatSource);
		result = SUCCEEDED(result) && guidPixelFormatSource != GUID_WICPixelFormat32bppBGRA
			? ConvertPixelFormat(bitmapSource, imagingFactory, &bitmapSourceConverted)
			: bitmapSource->QueryInterface(&bitmapSourceConverted); // No need to convert
		if (FAILED(result))
			return result;

		UINT nWidth, nHeight;
		if (result = bitmapSourceConverted->GetSize(&nWidth, &nHeight); FAILED(result))
		{
			bitmapSourceConverted->Release();
			return result;
		}
		imageSize.cx = static_cast<LONG>(nWidth);
		imageSize.cy = static_cast<LONG>(nHeight);

		BITMAPINFO bitmapInfo{};
		bitmapInfo.bmiHeader.biSize = sizeof bitmapInfo.bmiHeader;
		bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(nWidth);
		bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(nHeight);
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;

		BYTE* ppvBits;
		const auto newBitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, reinterpret_cast<void**>(&ppvBits), nullptr, 0);
		if (newBitmap == nullptr)
		{
			bitmapSourceConverted->Release();
			return E_OUTOFMEMORY;
		}

		const WICRect rect{ 0, 0, static_cast<INT>(nWidth), static_cast<INT>(nHeight) };
		if (result = bitmapSourceConverted->CopyPixels(&rect, nWidth * 4, nWidth * nHeight * 4, ppvBits); SUCCEEDED(result)) // It actually does conversion, not just copy. The converted pixels is store in newBitmap
			outConvertedBitmap = newBitmap;
		else
			DeleteObject(newBitmap);

		bitmapSourceConverted->Release();
		return result;
	}

	HRESULT WicCreate32BitsPerPixelHBitmap(IStream* stream, HBITMAP& outNewBitmap, WTS_ALPHATYPE& outAlphaType, SIZE& imageSize)
	{
		outNewBitmap = nullptr;

		IWICImagingFactory* imagingFactory;
		if (const auto result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imagingFactory)); FAILED(result))
			return result;

		IWICBitmapDecoder* decoder;
		if (const auto result = imagingFactory->CreateDecoderFromStream(stream, &GUID_VendorMicrosoft, WICDecodeMetadataCacheOnDemand, &decoder); FAILED(result))
		{
			imagingFactory->Release();
			return result;
		}

		IWICBitmapFrameDecode* bitmapFrameDecode;
		if (const auto result = decoder->GetFrame(0, &bitmapFrameDecode); FAILED(result))
		{
			decoder->Release();
			imagingFactory->Release();
			return result;
		}

		const auto result = ConvertBitmapSourceTo32BppHBitmap(bitmapFrameDecode, imagingFactory, outNewBitmap, imageSize);
		if (SUCCEEDED(result))
			outAlphaType = WTSAT_ARGB;

		bitmapFrameDecode->Release();
		decoder->Release();
		imagingFactory->Release();

		return result;
	}
	
	HRESULT LoadImageToHBitmap(const char* sourceImage, const size_t size, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType, SIZE& imageSize)
	{
		const Utility::DataIStream imageIStream{ sourceImage, size };
		const auto result = imageIStream.GetHResult();
		return SUCCEEDED(result)
			? WicCreate32BitsPerPixelHBitmap(imageIStream.GetIStream(), outBitmap, outAlphaType, imageSize)
			: result;
	}

	bool ImageSizeSatisfiesCoverConstraints(const SIZE& imageSize)
	{
		const auto width = imageSize.cx;
		const auto height = imageSize.cy;
		return width > 20 && height > 20 && (height >= width || (width / height < 2)); // If the width is two times or more of height then it is likely not the cover but some other image.
	}

	bool SaveImage(HBITMAP bitmapHandle, const wstring& fileName, const ImageFileType fileType)
	{
		ULONG_PTR gdiPlusToken;
		const GdiplusStartupInput gdi_plus_startup_input;
		GdiplusStartup(&gdiPlusToken, &gdi_plus_startup_input, nullptr);

		unique_ptr<Bitmap> image{ new Bitmap(bitmapHandle, nullptr)};

		CLSID encoderClsid;
		GetEncoderClsid(GetMimeType(fileType), &encoderClsid);

		image->Save(fileName.c_str(), &encoderClsid, nullptr);
		image.reset();

		GdiplusShutdown(gdiPlusToken);
		return true;
	}
	
	bool GetEncoderClsid(const wstring& mimeType, CLSID* outClsid)
	{
		unsigned encodersCount = 0;
		unsigned encodersSize = 0;

		GetImageEncodersSize(&encodersCount, &encodersSize);
		if (encodersSize == 0)
			return false;

		const unique_ptr<char[]> imageCodecInfo{ new char[encodersSize] };
		if (imageCodecInfo == nullptr)
			return false;
		const auto imageCodecInfoPtr = reinterpret_cast<ImageCodecInfo*>(imageCodecInfo.get());
		GetImageEncoders(encodersCount, encodersSize, imageCodecInfoPtr);

		for (UINT encoderIndex = 0; encoderIndex < encodersCount; ++encoderIndex)
		{
			if (!StrLib::EqualsCi(wstring{ imageCodecInfoPtr[encoderIndex].MimeType }, mimeType))
				continue;
			*outClsid = imageCodecInfoPtr[encoderIndex].Clsid;
			return true;			
		}

		return false;
	}

	const wchar_t* GetMimeType(const ImageFileType fileType)
	{
		switch (fileType)
		{
		case ImageFileType::Bmp: return L"image/bmp";
		case ImageFileType::Png: return L"image/png";
		case ImageFileType::Jpg: return L"image/jpg";
		default: return L"image/bmp";  // NOLINT(clang-diagnostic-covered-switch-default)
		}
	}
	


}
