#pragma once

#include <shobjidl.h>
#include <thumbcache.h>

#include "ComPtr.h"
#include "ReferenceCounter.h"

class LiveIcons final : public IInitializeWithStream, public IThumbnailProvider
{
	ReferenceCounter LiveIconsReferences{ 1 };
	ComPtr<IStream> Stream;

	LiveIcons() noexcept;

public:
	static HRESULT CreateInstance(REFIID riid, void** ppv) noexcept;

	LiveIcons(const LiveIcons&) = delete;
	LiveIcons(LiveIcons&&) = delete;
	LiveIcons& operator=(const LiveIcons&) = delete;
	LiveIcons& operator=(LiveIcons&&) = delete;
	~LiveIcons() noexcept;

	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override;
	IFACEMETHODIMP_(ULONG) AddRef() noexcept override;
	IFACEMETHODIMP_(ULONG) Release() noexcept override;

	IFACEMETHODIMP Initialize(IStream* stream, DWORD grfMode) noexcept override;
	IFACEMETHODIMP GetThumbnail(
		UINT cx,
		HBITMAP* outBitmapHandle,
		WTS_ALPHATYPE* outAlpha) noexcept override;
};
