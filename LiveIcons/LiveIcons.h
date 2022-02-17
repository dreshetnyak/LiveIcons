#pragma once

class LiveIcons final : public IInitializeWithStream, public IThumbnailProvider
{
    ReferenceCounter InstanceReferences{};
    IStream* Stream{};

	LiveIcons() = default;

public:
	static HRESULT CreateInstance(REFIID riid, void** ppv);

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
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override;
};

