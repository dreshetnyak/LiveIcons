#pragma once

#include <string>
#include <Windows.h>
#include <atlbase.h>
//#include <atlapp.h>
#include <atlimage.h>
#include <atlfile.h>
//#include <atlgdi.h>
#include <vector>
#include "GlobalMem.h"

namespace Gfx
{
	using namespace std;
	//using namespace WTL;
	using namespace Gdiplus;

	enum class ImageFileType { Bmp, Png, Jpg };

	bool LoadImage(CImage& outImage, const vector<char>& sourceImage);
	HBITMAP ToBitmap(CImage& sourceImage, LPSIZE bitmapSize);
	bool SaveImage(HBITMAP bitmapHandle, const wstring& fileName, ImageFileType fileType);
	bool GetEncoderClsid(const wstring& mimeType, CLSID* outClsid);
	const wchar_t* GetMimeType(ImageFileType fileType);
}