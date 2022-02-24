#include "pch.h"
#include "LiveIcons.h"
#include "Utility.h"

std::vector<std::shared_ptr<Parser::Base>> LiveIcons::Parsers
{
	std::make_shared<Parser::Epub>(Parser::Epub{})
};

LiveIcons::LiveIcons()
{
	Log::Write("LiveIcons::LiveIcons: Constructor. LiveIconsReferences: 1");
	static_cast<void>(LiveIconsReferences.Increment());
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

		Log::Write(std::format("LiveIcons::CreateInstance: Finished. HR: {}", std::system_category().message(result)));
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
		Log::Write(std::format("LiveIcons::QueryInterface: Finished. HR: {}", std::system_category().message(result)));
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
		const auto result = LiveIconsReferences.Increment();
		Log::Write(std::format("LiveIcons::AddRef: LiveIconsReferences: {}", result));
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
		const auto refCount = LiveIconsReferences.Decrement();
		Log::Write(std::format("LiveIcons::Release: LiveIconsReferences: {}", refCount));

		if (LiveIconsReferences.NoReference())
		{
			Log::Write("LiveIcons::Release: delete this");
			delete this;
		}
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
		Log::Write("LiveIcons::Initialize. Starting.");

		const auto result = Stream == nullptr
			? stream->QueryInterface(&Stream)
			: E_UNEXPECTED;

		Log::Write(std::format("LiveIcons::Initialize: Finished. HR: {}", std::system_category().message(result)));
		return result;
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
		Log::Write("LiveIcons::GetThumbnail: Starting.");
	
		const auto fileExtension = GetIStreamFileExtension(Stream);
		Log::Write(StrLib::ToString(std::format(L"LiveIcons::GetThumbnail: File extension: '{}'", fileExtension)));

		for (auto& parser : Parsers)
		{
			Log::Write("LiveIcons::GetThumbnail: parser->CanParse.");
			if (!parser->CanParse(fileExtension))
			{
				Log::Write("LiveIcons::GetThumbnail: Can't parse this file extension. Next parser.");
				continue;
			}

			Log::Write("LiveIcons::GetThumbnail: parser->Parse.");
			const auto&& parseResult = parser->Parse(Stream);
			if (FAILED(parseResult.HResult))
			{
				Log::Write(StrLib::ToString(std::format(L"LiveIcons::GetThumbnail: Parsing error: {}", parseResult.Error)));
				return parseResult.HResult;
			}

			Log::Write("LiveIcons::GetThumbnail: Parsing success. Cover image obtained.");
			*outBitmapHandle = parseResult.Cover;
			*putAlpha = parseResult.CoverAlpha;
			return S_OK;
		}

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

		Log::Write("LiveIcons::GetThumbnail: Failed to parse.");
		return E_FAIL;
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		Log::Write(std::format("LiveIcons::GetThumbnail: Exception: '{}'", message != nullptr ? message : ""));
		return E_UNEXPECTED;
	}
}

std::wstring LiveIcons::GetIStreamFileExtension(IStream *stream)
{
	if (stream == nullptr)
		return {};
	STATSTG streamStat{};
	if (const auto result = stream->Stat(&streamStat, STATFLAG_DEFAULT); FAILED(result))
		return {};
	const std::wstring fileName{ streamStat.pwcsName };
	const auto extensionOffset = fileName.find_last_of('.');
	return extensionOffset != std::string::npos
		? fileName.substr(extensionOffset)
		: std::wstring{};	
}
