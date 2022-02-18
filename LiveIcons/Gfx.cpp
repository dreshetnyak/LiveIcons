#include "pch.h"
#include "Gfx.h"


namespace Gfx
{
	bool LoadImage(CImage& outImage, const vector<char>& sourceImage)
	{
		const auto sourceImageSize = sourceImage.size();
		Utility::GlobalMem memory{sourceImageSize};
		if (!memory.Valid())
			return false;
		CopyMemory(memory.Ptr(), sourceImage.data(), sourceImageSize);
		memory.Unlock();

		IStream* stream = nullptr;
		if (FAILED(CreateStreamOnHGlobal(memory.Handle(), false, &stream)))
			return false;

		auto returnCode = true;
		try
		{
			outImage.Destroy();
			if (outImage.Load(stream) == E_FAIL)
				returnCode = false;
		}
		catch (...)
		{ returnCode = false; }

		stream->Release();
		return returnCode;
	}

	HBITMAP ToBitmap(CImage& sourceImage, LPSIZE bitmapSize)
	{

		return nullptr;

		//const auto src_width = sourceImage.GetWidth();
		//const auto src_height = sourceImage.GetHeight();
		//const auto bitmap_width = static_cast<float>(bitmapSize->cx);
		//const auto bitmap_height = static_cast<float>(bitmapSize->cy);
		//const auto src_to_bm_width = bitmap_width / static_cast<float>(src_width);
		//const auto src_to_bm_height = bitmap_height / static_cast<float>(src_height);

		//if (src_to_bm_width >= 1 && src_to_bm_height >= 1) // If the source image already fits into the bitmap and does not need scaling
		//	return sourceImage.Detach();


		/*
		// Scale down the image
		auto device_context = static_cast<CDC>(CreateCompatibleDC(nullptr));
		if (device_context.IsNull()) 
			return nullptr;
		device_context.SetStretchBltMode(HALFTONE);
		device_context.SetBrushOrg(0, 0, nullptr);

		const auto scaled_width = static_cast<int>((min(src_to_bm_width, src_to_bm_height) * static_cast<float>(src_width)));
		const auto scaled_height = static_cast<int>((min(src_to_bm_width, src_to_bm_height) * static_cast<float>(src_height)));
		
		// Create the new bitmap with the scaled down size
		CBitmap scaled_bitmap;
		scaled_bitmap.CreateCompatibleBitmap(sourceImage.GetDC(), scaled_width, scaled_height);
		sourceImage.ReleaseDC();
		if (scaled_bitmap.IsNull())
			return nullptr;

		const auto old_bitmap = device_context.SelectBitmap(scaled_bitmap);
		device_context.FillSolidRect(0, 0, scaled_width, scaled_height, RGB(255, 255, 255)); // White background

		*/

		//PIXELFORMATDESCRIPTOR ppfd{};
		//auto x = DescribePixelFormat(device_context, 1, sizeof(ppfd), &ppfd);
		//auto pixel_format = GetPixelFormat(device_context);

		//auto zz = device_context.BitBlt(0, 0, scaled_width, scaled_height, source_image.GetDC(), 0, 0, MERGECOPY);
		//scaled_bitmap.CreateBitmap(scaled_width, scaled_height, 1, 32, nullptr);
		//auto le = GetLastError();
		//device_context.DescribePixelFormat(PixelFormat32bppARGB, n)
		//PixelFormat32bppARGB
		
		//Creating nessecery palettes for gdi, so i can use Bitmap::FromHBITMAP
		//Bitmap* bitmap = Bitmap::FromHBITMAP(source_image, nullptr);
		
		////Getting data from the bitmap and creating new bitmap from it
		//Rect rect(0, 0, bitmap->GetWidth(), bitmap->GetHeight());
		//BitmapData bd;
		//bitmap->LockBits(&rect, ImageLockModeRead, bitmap->GetPixelFormat(), &bd);
		//bitmap = new Bitmap(bd.Width, bd.Height, bd.Stride, PixelFormat32bppARGB, (BYTE*)bd.Scan0); //Bitmap* newBitmapWithAplha
		//bitmap->UnlockBits(&bd);

		//auto pf = PixelFormat32bppARGB;
		//Bitmap bitmap{ scaled_width, scaled_height, PixelFormat32bppARGB };
		//bitmap.FromHBITMAP(source_image, nullptr);


		/*



		Bitmap bitmap{ static_cast<HBITMAP>(sourceImage), nullptr };
		if (bitmap.GetLastStatus() != Ok)
			return nullptr;
				
		


		Graphics gfx(device_context);
		gfx.SetInterpolationMode(InterpolationModeHighQualityBicubic);


		ColorMatrix color_matrix = 
		{
			1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 0.5f, 0.0f,
			0.0f, 0.0f, 0.0f, 0.0f, 1.0f
		};
		ImageAttributes image_attributes{};
		image_attributes.SetColorMatrix(&color_matrix);
		image_attributes.SetColorKey(Color(255, 255, 255), Color(255, 255, 255), ColorAdjustTypeBitmap);

		if (gfx.DrawImage(&bitmap, Rect{ 0, 0, scaled_width, scaled_height }, 0, 0, src_width, src_height, UnitPixel, &image_attributes) != Ok)
			return nullptr;
		
		////gfx.DrawImage(&image_file_type::bmp, destRect, 0, 0, width(), height(), Gdiplus::UnitPixel, &image_attributes);
		//WCHAR string[] = L"Sample Text";

		//// Initialize arguments.
		//Font myFont(L"Arial", 16);
		//PointF origin(0.0f, 0.0f);
		//SolidBrush blackBrush(Color(255, 0, 0, 0));

		//// Draw string.
		//gfx.DrawString(
		//	string,
		//	11,
		//	&myFont,
		//	origin,
		//	&blackBrush);
		
		//if (gfx.DrawImage(&bitmap, Rect{ 0, 0, scaled_width, scaled_height }, 0, 0, src_width, src_height, UnitPixel) != Ok)
		//	return nullptr;

		// TODO Not drawn if not scaling
		//if (m_showIcon)
		//	DrawIcon(device_context, 0, 0, zipIcon);

		device_context.SelectBitmap(old_bitmap);
		return scaled_bitmap.Detach();

		*/		
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

		const unique_ptr<char> imageCodecInfo{ new char[encodersSize] };
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








	HRESULT ConvertPixelFormat(IWICBitmapSource* bitmapSource, IWICImagingFactory* imagingFactory, IWICBitmapSource* bitmapSourceConverted)
	{
		IWICFormatConverter* formatConverter;		
		if (const auto result = imagingFactory->CreateFormatConverter(&formatConverter); FAILED(result))
			return result;
		Utility::OnDestructor releaseImagingFactory{ [&]() -> void { formatConverter->Release(); } };
	
		// Create the appropriate pixel format converter
		const auto result = formatConverter->Initialize(bitmapSource, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
		return SUCCEEDED(result)
			? formatConverter->QueryInterface(&bitmapSourceConverted)
			: result;
	}

	HRESULT ConvertBitmapSourceTo32BppHBitmap(IWICBitmapSource* bitmapSource, IWICImagingFactory* imagingFactory, HBITMAP* outConvertedBitmap)
	{
		*outConvertedBitmap = nullptr;

		IWICBitmapSource* bitmapSourceConverted = nullptr;
		WICPixelFormatGUID guidPixelFormatSource;

		auto result = bitmapSource->GetPixelFormat(&guidPixelFormatSource);
		result = SUCCEEDED(result) && guidPixelFormatSource != GUID_WICPixelFormat32bppBGRA
			? ConvertPixelFormat(bitmapSource, imagingFactory, bitmapSourceConverted)
			: bitmapSource->QueryInterface(&bitmapSourceConverted); // No need to convert
		if (FAILED(result))
			return result;
		Utility::OnDestructor releaseDecoder{ [&]() -> void { bitmapSourceConverted->Release(); } };

		UINT nWidth, nHeight;
		if (result = bitmapSourceConverted->GetSize(&nWidth, &nHeight); FAILED(result))
			return result;
				
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
			return E_OUTOFMEMORY;

		const WICRect rect{ 0, 0, static_cast<INT>(nWidth), static_cast<INT>(nHeight) };
		if (result = bitmapSourceConverted->CopyPixels(&rect, nWidth * 4, nWidth * nHeight * 4, ppvBits); SUCCEEDED(result)) // It actually does conversion, not just copy. The converted pixels is store in newBitmap
			*outConvertedBitmap = newBitmap;
		else
			DeleteObject(newBitmap);

		return result;
	}

	HRESULT WicCreate32BitsPerPixelHBitmap(IStream* stream, HBITMAP* outNewBitmap, WTS_ALPHATYPE* outAlphaType)
	{
		*outNewBitmap = nullptr;

		IWICImagingFactory* imagingFactory;		
		if (const auto result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imagingFactory)); FAILED(result))
			return result;
		Utility::OnDestructor releaseImagingFactory{ [&]() -> void { imagingFactory->Release(); } };

		IWICBitmapDecoder* decoder;
		if (const auto result = imagingFactory->CreateDecoderFromStream(stream, &GUID_VendorMicrosoft, WICDecodeMetadataCacheOnDemand, &decoder); FAILED(result))
			return result;
		Utility::OnDestructor releaseDecoder{ [&]() -> void { decoder->Release(); } };
		
		IWICBitmapFrameDecode* bitmapFrameDecode;
		if (const auto result = decoder->GetFrame(0, &bitmapFrameDecode); FAILED(result))
			return result;
		Utility::OnDestructor releaseBitmapFrameDecode{ [&]() -> void { bitmapFrameDecode->Release(); } };

		const auto result = ConvertBitmapSourceTo32BppHBitmap(bitmapFrameDecode, imagingFactory, outNewBitmap);
		if (SUCCEEDED(result))
			*outAlphaType = WTSAT_ARGB;
		
		return result;
	}


}
