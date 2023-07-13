#pragma once
#include "ParserBase.h"
#include "ParserEpub.h"
#include "ReferenceCounter.h"

class LiveIcons : public IInitializeWithStream, public IThumbnailProvider
{
    static std::vector<std::shared_ptr<Parser::Base>> Parsers;
	ReferenceCounter LiveIconsReferences{};
    IStream* Stream{};

	LiveIcons();
    static std::wstring GetIStreamFileExtension(IStream* stream);

public:
	static HRESULT CreateInstance(REFIID riid, void** ppv);
    static const std::vector<std::shared_ptr<Parser::Base>>& GetParsers() { return Parsers; }

    LiveIcons(const LiveIcons& liveIcons) = delete;
    LiveIcons(LiveIcons&& liveIcons) = delete;
    virtual ~LiveIcons();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD grfMode) override;

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* outBitmapHandle, WTS_ALPHATYPE* putAlpha) override;
};
