#include "pch.h"
#include "LiveIcons.h"

HRESULT LiveIcons::CreateInstance(const IID& riid, void** ppv)
{
	const auto instance = new (std::nothrow) LiveIcons{};
    if (instance == nullptr)
		return E_OUTOFMEMORY;
	const auto result = instance->QueryInterface(riid, ppv);
    instance->Release();
    return result;
}

LiveIcons::~LiveIcons()
{
	if (Stream)
		Stream->Release();
}

///////////////////////////
// IUnknown Methods

IFACEMETHODIMP LiveIcons::QueryInterface(REFIID riid, void** ppv)
{
	static const QITAB QIT[] =
	{
		QITABENT(LiveIcons, IInitializeWithStream),
		QITABENT(LiveIcons, IThumbnailProvider),
		{ nullptr, 0 }
	};

	return QISearch(this, QIT, riid, ppv);
}

ULONG LiveIcons::AddRef()
{
	return InstanceReferences.Increment();
}

ULONG LiveIcons::Release()
{
	const auto refCount = InstanceReferences.Decrement();
	if (InstanceReferences.NoReference())
		delete this;
	return refCount;
}

///////////////////////////
// IInitializeWithStream

IFACEMETHODIMP LiveIcons::Initialize(IStream* stream, DWORD)
{
	return stream == nullptr
		? stream->QueryInterface(&Stream)
		: E_UNEXPECTED;
}

///////////////////////////
// IThumbnailProvider

IFACEMETHODIMP LiveIcons::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
{
	//PWSTR pszBase64EncodedImageString;
	//HRESULT hr = _GetBase64EncodedImageString(cx, &pszBase64EncodedImageString);
	//if (SUCCEEDED(hr))
	//{
	//	IStream* pImageStream;
	//	hr = _GetStreamFromString(pszBase64EncodedImageString, &pImageStream);
	//	if (SUCCEEDED(hr))
	//	{
	//		hr = WICCreate32BitsPerPixelHBITMAP(pImageStream, cx, phbmp, pdwAlpha);;
	//		pImageStream->Release();
	//	}
	//	CoTaskMemFree(pszBase64EncodedImageString);
	//}
	//return hr;

	return E_UNEXPECTED;
}