#pragma once

#include <cstddef>
#include <Windows.h>
#include <thumbcache.h>

namespace Gfx
{
	HRESULT LoadImageToHBitmap(
		const char* sourceImage,
		std::size_t size,
		HBITMAP& outBitmap,
		WTS_ALPHATYPE& outAlphaType,
		SIZE& imageSize) noexcept;

	[[nodiscard]] bool ImageSizeSatisfiesCoverConstraints(const SIZE& imageSize) noexcept;
}
