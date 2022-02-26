#include "pch.h"
#include "LiveIcons.h"
#include "ParserFb2.h"
#include "Utility.h"

std::vector<std::shared_ptr<Parser::Base>> LiveIcons::Parsers
{
	std::make_shared<Parser::Epub>(Parser::Epub{}),
	std::make_shared<Parser::Fb2>(Parser::Fb2{})
};

LiveIcons::LiveIcons()
{
	static_cast<void>(LiveIconsReferences.Increment());
}

HRESULT LiveIcons::CreateInstance(const IID& riid, void** ppv)
{
	try
	{
		const auto instance = new (std::nothrow) LiveIcons{};
		if (instance == nullptr)
			return E_OUTOFMEMORY;
		const auto result = instance->QueryInterface(riid, ppv);
		instance->Release();
		return result;
	}
	catch (...)
	{ return E_UNEXPECTED; }
}

LiveIcons::~LiveIcons()
{
	try
	{
		if (Stream)
			Stream->Release();
	}
	catch (...)
	{ /* ignore */ }
}

///////////////////////////
// IUnknown Methods

IFACEMETHODIMP LiveIcons::QueryInterface(REFIID riid, void** ppv)
{
	try
	{
		static const QITAB QIT[] =
		{
			QITABENT(LiveIcons, IInitializeWithStream),
			QITABENT(LiveIcons, IThumbnailProvider),
			{ nullptr, 0 }
		};

		const auto result = QISearch(this, QIT, riid, ppv);
		return result;
	}
	catch (...)
	{
		return E_UNEXPECTED;
	}
}

ULONG LiveIcons::AddRef()
{
	try
	{
		const auto result = LiveIconsReferences.Increment();
		return result;
	}
	catch (...)
	{ return 0;	}
}

ULONG LiveIcons::Release()
{
	try
	{
		const auto refCount = LiveIconsReferences.Decrement();

		if (LiveIconsReferences.NoReference())
			delete this;
		return refCount;
	}
	catch (...)
	{ return 0; }
}

///////////////////////////
// IInitializeWithStream

IFACEMETHODIMP LiveIcons::Initialize(IStream* stream, DWORD)
{
	try
	{
		const auto result = Stream == nullptr
			? stream->QueryInterface(&Stream)
			: E_UNEXPECTED;

		return result;
	}
	catch (...)
	{ return E_UNEXPECTED; }
}

///////////////////////////
// IThumbnailProvider

IFACEMETHODIMP LiveIcons::GetThumbnail(UINT cx, HBITMAP* outBitmapHandle, WTS_ALPHATYPE* putAlpha)
{
	try
	{
		Log::Write("LiveIcons::GetThumbnail: Starting.");

		std::wstring fileName{};
		if (const auto result = Utility::GetIStreamFileName(Stream, fileName); FAILED(result))
			return E_FAIL;
		Log::Write(StrLib::ToString(std::format(L"LiveIcons::GetThumbnail: File name: '{}'", fileName)));

		std::wstring fileExtension{};
		if (const auto result = Utility::GetFileExtension(fileName, fileExtension); FAILED(result))
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