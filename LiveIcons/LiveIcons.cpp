#include "pch.h"
#include "LiveIcons.h"
#include "Utility.h"

LiveIcons::LiveIcons()
{
	Log::Write("LiveIcons::LiveIcons: Constructor.");
}

HRESULT LiveIcons::CreateInstance(const IID& riid, void** ppv)
{
	try
	{
		Log::Write("LiveIcons::CreateInstance: Starting.");

		const auto instance = new (std::nothrow) LiveIcons{};
		if (instance == nullptr)
		{
			Log::Write("LiveIcons::CreateInstance: Error: E_OUTOFMEMORY");
			return E_OUTOFMEMORY;
		}
		const auto result = instance->QueryInterface(riid, ppv);
		instance->Release();

		Log::Write(std::format("LiveIcons::CreateInstance: Finished. HRESULT: {}", std::system_category().message(result)));
		return result;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("LiveIcons::CreateInstance: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

LiveIcons::~LiveIcons()
{
	try
	{
		Log::Write("LiveIcons::~LiveIcons: Destructor.");

		if (Stream)
			Stream->Release();
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("LiveIcons::~LiveIcons: Exception: '{}'", message != nullptr ? message : ""));
	}
}

///////////////////////////
// IUnknown Methods

IFACEMETHODIMP LiveIcons::QueryInterface(REFIID riid, void** ppv)
{
	try
	{
		Log::Write("LiveIcons::QueryInterface.");

		static const QITAB QIT[] =
		{
			QITABENT(LiveIcons, IInitializeWithStream),
			QITABENT(LiveIcons, IThumbnailProvider),
			{ nullptr, 0 }
		};

		const auto result = QISearch(this, QIT, riid, ppv);
		Log::Write(std::format("LiveIcons::QueryInterface: Finished. HRESULT: {}", std::system_category().message(result)));
		return result;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("LiveIcons::QueryInterface: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

ULONG LiveIcons::AddRef()
{
	try
	{
		const auto result = InstanceReferences.Increment();
		Log::Write(std::format("LiveIcons::AddRef: {}", result));
		return result;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("LiveIcons::AddRef: Exception: '{}'", message != nullptr ? message : ""));
		return 0;
	}
}

ULONG LiveIcons::Release()
{
	try
	{
		const auto refCount = InstanceReferences.Decrement();
		Log::Write(std::format("LiveIcons::Release: {}", refCount));

		if (InstanceReferences.NoReference())
			delete this;
		return refCount;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("LiveIcons::Release: Exception: '{}'", message != nullptr ? message : ""));
		return 0;
	}
}

///////////////////////////
// IInitializeWithStream

IFACEMETHODIMP LiveIcons::Initialize(IStream* stream, DWORD)
{
	try
	{
		Log::Write("LiveIcons::Initialize.");

		return stream == nullptr
			? stream->QueryInterface(&Stream)
			: E_UNEXPECTED;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("LiveIcons::Initialize: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

///////////////////////////
// IThumbnailProvider

IFACEMETHODIMP LiveIcons::GetThumbnail(UINT cx, HBITMAP* outBitmapHandle, WTS_ALPHATYPE* putAlpha)
{
	try
	{
		Log::Write("LiveIcons::GetThumbnail.");

		STATSTG streamStat{};
		if (const auto result = Stream->Stat(&streamStat, STATFLAG_DEFAULT); FAILED(result))
		{
			Log::Write("LiveIcons::GetThumbnail. Stream->Stat. Error.");
			return result;
		}

		Log::Write(std::format("GetThumbnail: {}", StrLib::ToString(streamStat.pwcsName)));


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

		return E_UNEXPECTED; //
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("LiveIcons::GetThumbnail: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}

}