#include "pch.h"
#include "LiveIcons.h"

#include <array>

#include "DllMain.h"
#include "ExceptionBoundary.h"
#include "Log.h"
#include "ParserCbr.h"
#include "ParserChm.h"
#include "ParserEpub.h"
#include "ParserFb2.h"
#include "ParserMobi.h"
#include "Utility.h"

namespace
{
	struct ParserEntry final
	{
		const char* Name;
		Parser::Base* Instance;
	};
}

LiveIcons::LiveIcons() noexcept
{
	static_cast<void>(dllReferenceCounter.Increment());
}

HRESULT LiveIcons::CreateInstance(const IID& riid, void** ppv) noexcept
{
	if (ppv == nullptr)
		return E_POINTER;
	*ppv = nullptr;

	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::LiveIconsCreate,
		correlationId,
		[&]() -> HRESULT
		{
			auto* const instance = new (std::nothrow) LiveIcons{};
			if (instance == nullptr)
			{
				Log::Error(
					Log::EventId::LiveIconsCreate,
					E_OUTOFMEMORY,
					correlationId,
					"plugin-allocation");
				return E_OUTOFMEMORY;
			}

			const HRESULT result = instance->QueryInterface(riid, ppv);
			instance->Release();
			if (FAILED(result))
				Log::Error(
					Log::EventId::LiveIconsCreate,
					result,
					correlationId,
					"plugin-query-interface");
			return result;
		});
}

LiveIcons::~LiveIcons() noexcept
{
	Stream.Reset();
	static_cast<void>(dllReferenceCounter.Decrement());
}

HRESULT LiveIcons::QueryInterface(REFIID riid, void** ppv) noexcept
{
	if (ppv == nullptr)
		return E_POINTER;
	*ppv = nullptr;

	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::LiveIconsQueryInterface,
		correlationId,
		[&]() -> HRESULT
		{
			static const QITAB interfaces[]
			{
				QITABENT(LiveIcons, IInitializeWithStream),
				QITABENT(LiveIcons, IThumbnailProvider),
				{ nullptr, 0 }
			};
			const HRESULT result = QISearch(this, interfaces, riid, ppv);
			if (FAILED(result))
				Log::Diagnostic(
					Log::EventId::LiveIconsQueryInterface,
					result,
					correlationId,
					"interface-not-supported");
			return result;
		});
}

ULONG LiveIcons::AddRef() noexcept
{
	return LiveIconsReferences.Increment();
}

ULONG LiveIcons::Release() noexcept
{
	const auto references = LiveIconsReferences.Decrement();
	if (references == 0)
		delete this;
	return references;
}

HRESULT LiveIcons::Initialize(IStream* stream, DWORD) noexcept
{
	if (stream == nullptr)
		return E_POINTER;
	if (Stream)
		return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::LiveIconsInitialize,
		correlationId,
		[&]() -> HRESULT
		{
			ComPtr<IStream> initializedStream;
			IStream** const output = initializedStream.Put();
			const HRESULT result = stream->QueryInterface(
				IID_IStream, reinterpret_cast<void**>(output));
			if (FAILED(result))
			{
				Log::Error(
					Log::EventId::LiveIconsInitialize,
					result,
					correlationId,
					"stream-query-interface");
				return result;
			}
			if (!initializedStream)
			{
				Log::Error(
					Log::EventId::LiveIconsInitialize,
					E_UNEXPECTED,
					correlationId,
					"success-without-stream");
				return E_UNEXPECTED;
			}

			Stream = std::move(initializedStream);
			return S_OK;
		});
}

HRESULT LiveIcons::GetThumbnail(
	UINT,
	HBITMAP* outBitmapHandle,
	WTS_ALPHATYPE* outAlpha) noexcept
{
	if (outBitmapHandle != nullptr)
		*outBitmapHandle = nullptr;
	if (outAlpha != nullptr)
		*outAlpha = WTSAT_UNKNOWN;
	if (outBitmapHandle == nullptr || outAlpha == nullptr)
		return E_POINTER;
	if (!Stream)
		return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);

	const auto correlationId = Log::NextCorrelationId();
	return ExceptionBoundary::ToHResult(
		Log::EventId::Thumbnail,
		correlationId,
		[&]() -> HRESULT
		{
			std::wstring fileName;
			if (const HRESULT result = Utility::GetIStreamFileName(Stream.Get(), fileName);
				FAILED(result))
			{
				Log::Error(
					Log::EventId::Thumbnail,
					result,
					correlationId,
					"stream-name");
				return result;
			}

			std::wstring fileExtension;
			if (const HRESULT result = Utility::GetFileExtension(fileName, fileExtension);
				FAILED(result))
			{
				Log::Error(
					Log::EventId::Thumbnail,
					result,
					correlationId,
					"file-extension");
				return result;
			}

			Parser::Epub epub;
			Parser::Fb2 fb2;
			Parser::Mobi mobi;
			Parser::Chm chm;
			Parser::Cbr cbr;
			const std::array parsers
			{
				ParserEntry{ "epub", &epub },
				ParserEntry{ "fb2", &fb2 },
				ParserEntry{ "mobi", &mobi },
				ParserEntry{ "chm", &chm },
				ParserEntry{ "cbr", &cbr }
			};

			for (const auto& parser : parsers)
			{
				if (!parser.Instance->CanParse(fileExtension))
					continue;

				auto parseResult = parser.Instance->Parse(Stream.Get());
				if (FAILED(parseResult.HResult))
				{
					Log::Error(
						Log::EventId::Thumbnail,
						parseResult.HResult,
						correlationId,
						parser.Name);
					return parseResult.HResult;
				}
				if (parseResult.GetCover() == nullptr)
				{
					Log::Error(
						Log::EventId::Thumbnail,
						E_UNEXPECTED,
						correlationId,
						"success-without-cover");
					return E_UNEXPECTED;
				}

				*outAlpha = parseResult.CoverAlpha;
				*outBitmapHandle = parseResult.ReleaseCover();
				Log::Diagnostic(
					Log::EventId::Thumbnail,
					S_OK,
					correlationId,
					parser.Name);
				return S_OK;
			}

			const HRESULT result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
			Log::Diagnostic(
				Log::EventId::Thumbnail,
				result,
				correlationId,
				"unsupported-extension");
			return result;
		});
}
