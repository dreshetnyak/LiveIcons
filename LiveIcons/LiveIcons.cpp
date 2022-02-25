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

		std::wstring fileExtension{};
		if (const auto result = Utility::GetIStreamFileExtension(Stream, fileExtension); FAILED(result))
			return E_FAIL;

		Log::Write(StrLib::ToString(std::format(L"LiveIcons::GetThumbnail: File extension: '{}'", fileExtension)));

		for (const auto& parser : Parsers)
		{
			if (!parser->CanParse(fileExtension))
				continue;

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


		/*
		// Draw the image
		const auto bitmap = Gfx::ToBitmap(cImage, unique_ptr<SIZE>{ new SIZE{ 256, 256 } }.get());

		// TODO DEBUG CODE
		const auto fileNameOffset = StrLib::FindReverse(filePath, L'\\');
		const auto fileName = fileNameOffset != wstring::npos ? filePath.substr(fileNameOffset + 1) : filePath;
		Gfx::SaveImage(bitmap, L"R:\\Temp\\EPUBX\\" + fileName + L".png", Gfx::ImageFileType::Png);
		// TODO DEBUG CODE
		*/

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