#include "pch.h"
#include "Gfx.h"

#include <atlbase.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace Gfx
{
	namespace
	{
		constexpr std::uint64_t BytesPerPixel{ 4 };
		constexpr std::uint64_t MaximumEncodedImageBytes{ 64ULL * 1024ULL * 1024ULL };
		constexpr std::uint64_t MaximumDecodedBitmapBytes{ 64ULL * 1024ULL * 1024ULL };

		// Borrows a stable caller-owned buffer. LoadImageToHBitmap keeps the
		// buffer alive until WIC and every stream clone have been released.
		class ReadOnlyMemoryStream final : public IStream
		{
			std::atomic<ULONG> References{ 1 };
			const BYTE* Data;
			ULONGLONG Size;
			ULONGLONG Position{};

			explicit ReadOnlyMemoryStream(
				const BYTE* const data, const ULONGLONG size) noexcept :
				Data{ data }, Size{ size }
			{
			}

			~ReadOnlyMemoryStream() noexcept = default;

		public:
			[[nodiscard]] static HRESULT Create(
				const char* const data, const std::size_t size, IStream** const outStream) noexcept
			{
				if (outStream == nullptr)
					return E_POINTER;
				*outStream = nullptr;
				if (data == nullptr)
					return E_POINTER;
				if (size == 0)
					return E_INVALIDARG;

				auto* const stream = new (std::nothrow) ReadOnlyMemoryStream{
					reinterpret_cast<const BYTE*>(data), static_cast<ULONGLONG>(size) };
				if (stream == nullptr)
					return E_OUTOFMEMORY;
				*outStream = stream;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(
				REFIID interfaceId, void** const outObject) noexcept override
			{
				if (outObject == nullptr)
					return E_POINTER;
				*outObject = nullptr;
				if (!IsEqualIID(interfaceId, IID_IUnknown) &&
					!IsEqualIID(interfaceId, IID_ISequentialStream) &&
					!IsEqualIID(interfaceId, IID_IStream))
					return E_NOINTERFACE;
				*outObject = static_cast<IStream*>(this);
				AddRef();
				return S_OK;
			}

			ULONG STDMETHODCALLTYPE AddRef() noexcept override
			{
				return References.fetch_add(1, std::memory_order_relaxed) + 1;
			}

			ULONG STDMETHODCALLTYPE Release() noexcept override
			{
				const ULONG remaining = References.fetch_sub(1, std::memory_order_acq_rel) - 1;
				if (remaining == 0)
					delete this;
				return remaining;
			}

			HRESULT STDMETHODCALLTYPE Read(
				void* const buffer, const ULONG requested, ULONG* const outRead) noexcept override
			{
				if (outRead != nullptr)
					*outRead = 0;
				if (buffer == nullptr && requested != 0)
					return STG_E_INVALIDPOINTER;

				const ULONGLONG remaining = Position < Size ? Size - Position : 0;
				const ULONG byteCount = static_cast<ULONG>((std::min)(
					remaining, static_cast<ULONGLONG>(requested)));
				if (byteCount != 0)
				{
					if (buffer == nullptr)
						return STG_E_INVALIDPOINTER;
					std::memcpy(buffer, Data + static_cast<std::size_t>(Position), byteCount);
					Position += byteCount;
				}
				if (outRead != nullptr)
					*outRead = byteCount;
				return byteCount == requested ? S_OK : S_FALSE;
			}

			HRESULT STDMETHODCALLTYPE Write(
				const void*, const ULONG, ULONG* const outWritten) noexcept override
			{
				if (outWritten != nullptr)
					*outWritten = 0;
				return STG_E_ACCESSDENIED;
			}

			HRESULT STDMETHODCALLTYPE Seek(
				const LARGE_INTEGER move, const DWORD origin,
				ULARGE_INTEGER* const outPosition) noexcept override
			{
				ULONGLONG base{};
				switch (origin)
				{
				case STREAM_SEEK_SET: base = 0; break;
				case STREAM_SEEK_CUR: base = Position; break;
				case STREAM_SEEK_END: base = Size; break;
				default: return STG_E_INVALIDFUNCTION;
				}

				ULONGLONG target{};
				if (move.QuadPart < 0)
				{
					const ULONGLONG magnitude =
						static_cast<ULONGLONG>(-(move.QuadPart + 1)) + 1;
					if (magnitude > base)
						return STG_E_INVALIDFUNCTION;
					target = base - magnitude;
				}
				else
				{
					const ULONGLONG positiveMove = static_cast<ULONGLONG>(move.QuadPart);
					if (base > (std::numeric_limits<ULONGLONG>::max)() - positiveMove)
						return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
					target = base + positiveMove;
				}

				Position = target;
				if (outPosition != nullptr)
					outPosition->QuadPart = Position;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE SetSize(const ULARGE_INTEGER) noexcept override
			{
				return STG_E_ACCESSDENIED;
			}

			HRESULT STDMETHODCALLTYPE CopyTo(
				IStream* const destination, const ULARGE_INTEGER requested,
				ULARGE_INTEGER* const outRead,
				ULARGE_INTEGER* const outWritten) noexcept override
			{
				if (outRead != nullptr)
					outRead->QuadPart = 0;
				if (outWritten != nullptr)
					outWritten->QuadPart = 0;
				if (destination == nullptr)
					return STG_E_INVALIDPOINTER;

				const ULONGLONG available = Position < Size ? Size - Position : 0;
				const ULONGLONG total = (std::min)(available, requested.QuadPart);
				ULONGLONG copied{};
				while (copied < total)
				{
					const ULONG chunk = static_cast<ULONG>((std::min)(
						total - copied,
						static_cast<ULONGLONG>((std::numeric_limits<ULONG>::max)())));
					ULONG written{};
					HRESULT result{};
					try
					{
						result = destination->Write(
							Data + static_cast<std::size_t>(Position), chunk, &written);
					}
					catch (const std::bad_alloc&)
					{
						return E_OUTOFMEMORY;
					}
					catch (...)
					{
						return E_UNEXPECTED;
					}
					if (written > chunk)
						return E_UNEXPECTED;
					Position += written;
					copied += written;
					if (outRead != nullptr)
						outRead->QuadPart = copied;
					if (outWritten != nullptr)
						outWritten->QuadPart = copied;
					if (FAILED(result))
						return result;
					if (written != chunk)
						return STG_E_MEDIUMFULL;
				}
				return total == requested.QuadPart ? S_OK : S_FALSE;
			}

			HRESULT STDMETHODCALLTYPE Commit(const DWORD) noexcept override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Revert() noexcept override
			{
				return STG_E_REVERTED;
			}

			HRESULT STDMETHODCALLTYPE LockRegion(
				const ULARGE_INTEGER, const ULARGE_INTEGER, const DWORD) noexcept override
			{
				return STG_E_INVALIDFUNCTION;
			}

			HRESULT STDMETHODCALLTYPE UnlockRegion(
				const ULARGE_INTEGER, const ULARGE_INTEGER, const DWORD) noexcept override
			{
				return STG_E_INVALIDFUNCTION;
			}

			HRESULT STDMETHODCALLTYPE Stat(
				STATSTG* const statistics, const DWORD) noexcept override
			{
				if (statistics == nullptr)
					return STG_E_INVALIDPOINTER;
				*statistics = {};
				statistics->type = STGTY_STREAM;
				statistics->cbSize.QuadPart = Size;
				statistics->grfMode = STGM_READ;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Clone(IStream** const outStream) noexcept override
			{
				if (outStream == nullptr)
					return E_POINTER;
				*outStream = nullptr;
				auto* const clone = new (std::nothrow) ReadOnlyMemoryStream{ Data, Size };
				if (clone == nullptr)
					return E_OUTOFMEMORY;
				clone->Position = Position;
				*outStream = clone;
				return S_OK;
			}
		};

		struct BitmapDeleter final
		{
			void operator()(HBITMAP bitmap) const noexcept
			{
				if (bitmap != nullptr)
					DeleteObject(bitmap);
			}
		};

		using UniqueBitmap = std::unique_ptr<std::remove_pointer_t<HBITMAP>, BitmapDeleter>;

		HRESULT ConvertPixelFormat(
			IWICBitmapSource* bitmapSource,
			IWICImagingFactory* imagingFactory,
			CComPtr<IWICBitmapSource>& outConvertedSource) noexcept
		{
			outConvertedSource.Release();
			if (bitmapSource == nullptr || imagingFactory == nullptr)
				return E_POINTER;

			CComPtr<IWICFormatConverter> formatConverter;
			HRESULT result = imagingFactory->CreateFormatConverter(&formatConverter);
			if (FAILED(result))
				return result;

			result = formatConverter->Initialize(
				bitmapSource,
				GUID_WICPixelFormat32bppBGRA,
				WICBitmapDitherTypeNone,
				nullptr,
				0,
				WICBitmapPaletteTypeCustom);
			if (FAILED(result))
				return result;

			return formatConverter->QueryInterface(&outConvertedSource);
		}

		HRESULT Get32BppSource(
			IWICBitmapSource* bitmapSource,
			IWICImagingFactory* imagingFactory,
			CComPtr<IWICBitmapSource>& outConvertedSource) noexcept
		{
			outConvertedSource.Release();
			if (bitmapSource == nullptr || imagingFactory == nullptr)
				return E_POINTER;

			WICPixelFormatGUID sourcePixelFormat{};
			const HRESULT result = bitmapSource->GetPixelFormat(&sourcePixelFormat);
			if (FAILED(result))
				return result;

			return sourcePixelFormat == GUID_WICPixelFormat32bppBGRA
				? bitmapSource->QueryInterface(&outConvertedSource)
				: ConvertPixelFormat(bitmapSource, imagingFactory, outConvertedSource);
		}

		HRESULT ConvertBitmapSourceTo32BppHBitmap(
			IWICBitmapSource* bitmapSource,
			IWICImagingFactory* imagingFactory,
			HBITMAP& outConvertedBitmap,
			SIZE& outImageSize) noexcept
		{
			outConvertedBitmap = nullptr;
			outImageSize = {};
			if (bitmapSource == nullptr || imagingFactory == nullptr)
				return E_POINTER;

			CComPtr<IWICBitmapSource> convertedSource;
			HRESULT result = Get32BppSource(bitmapSource, imagingFactory, convertedSource);
			if (FAILED(result))
				return result;

			UINT sourceWidth = 0;
			UINT sourceHeight = 0;
			result = convertedSource->GetSize(&sourceWidth, &sourceHeight);
			if (FAILED(result))
				return result;

			const auto width = static_cast<std::uint64_t>(sourceWidth);
			const auto height = static_cast<std::uint64_t>(sourceHeight);
			const auto maximumLong = static_cast<std::uint64_t>((std::numeric_limits<LONG>::max)());
			const auto maximumPixels = MaximumDecodedBitmapBytes / BytesPerPixel;
			if (width == 0 || height == 0)
				return WINCODEC_ERR_BADIMAGE;
			if (width > maximumLong || height > maximumLong || width > maximumPixels / height)
			{
				return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
			}

			const auto stride = width * BytesPerPixel;
			const auto bufferSize = stride * height;
			if (stride > (std::numeric_limits<UINT>::max)() ||
				bufferSize > (std::numeric_limits<UINT>::max)())
			{
				return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
			}

			BITMAPINFO bitmapInfo{};
			bitmapInfo.bmiHeader.biSize = sizeof bitmapInfo.bmiHeader;
			bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(sourceWidth);
			bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(sourceHeight);
			bitmapInfo.bmiHeader.biPlanes = 1;
			bitmapInfo.bmiHeader.biBitCount = 32;
			bitmapInfo.bmiHeader.biCompression = BI_RGB;

			BYTE* bitmapBits = nullptr;
			SetLastError(ERROR_SUCCESS);
			UniqueBitmap bitmap{ CreateDIBSection(
				nullptr,
				&bitmapInfo,
				DIB_RGB_COLORS,
				reinterpret_cast<void**>(&bitmapBits),
				nullptr,
				0) };
			if (!bitmap)
			{
				const DWORD error = GetLastError();
				return error == ERROR_SUCCESS ? E_OUTOFMEMORY : HRESULT_FROM_WIN32(error);
			}
			if (bitmapBits == nullptr)
				return E_UNEXPECTED;

			const WICRect sourceRectangle{
				0,
				0,
				static_cast<INT>(sourceWidth),
				static_cast<INT>(sourceHeight) };
			result = convertedSource->CopyPixels(
				&sourceRectangle,
				static_cast<UINT>(stride),
				static_cast<UINT>(bufferSize),
				bitmapBits);
			if (FAILED(result))
				return result;

			outImageSize = {
				static_cast<LONG>(sourceWidth),
				static_cast<LONG>(sourceHeight) };
			outConvertedBitmap = bitmap.release();
			return S_OK;
		}

		HRESULT WicCreate32BitsPerPixelHBitmap(
			IStream* stream,
			HBITMAP& outNewBitmap,
			WTS_ALPHATYPE& outAlphaType,
			SIZE& outImageSize) noexcept
		{
			outNewBitmap = nullptr;
			outAlphaType = WTSAT_UNKNOWN;
			outImageSize = {};
			if (stream == nullptr)
				return E_POINTER;

			CComPtr<IWICImagingFactory> imagingFactory;
			HRESULT result = CoCreateInstance(
				CLSID_WICImagingFactory,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&imagingFactory));
			if (FAILED(result))
				return result;

			CComPtr<IWICBitmapDecoder> decoder;
			result = imagingFactory->CreateDecoderFromStream(
				stream,
				&GUID_VendorMicrosoft,
				WICDecodeMetadataCacheOnDemand,
				&decoder);
			if (FAILED(result))
				return result;

			CComPtr<IWICBitmapFrameDecode> bitmapFrame;
			result = decoder->GetFrame(0, &bitmapFrame);
			if (FAILED(result))
				return result;

			result = ConvertBitmapSourceTo32BppHBitmap(
				bitmapFrame,
				imagingFactory,
				outNewBitmap,
				outImageSize);
			if (SUCCEEDED(result))
				outAlphaType = WTSAT_ARGB;

			return result;
		}
	}

	HRESULT LoadImageToHBitmap(
		const char* sourceImage,
		const std::size_t size,
		HBITMAP& outBitmap,
		WTS_ALPHATYPE& outAlphaType,
		SIZE& imageSize) noexcept
	{
		outBitmap = nullptr;
		outAlphaType = WTSAT_UNKNOWN;
		imageSize = {};

		if (sourceImage == nullptr)
			return E_POINTER;
		if (size == 0)
			return E_INVALIDARG;
		if (size > MaximumEncodedImageBytes)
			return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

		try
		{
			CComPtr<IStream> imageStream;
			const HRESULT result = ReadOnlyMemoryStream::Create(
				sourceImage, size, &imageStream);
			return FAILED(result)
				? result
				: WicCreate32BitsPerPixelHBitmap(
					imageStream, outBitmap, outAlphaType, imageSize);
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_FAIL;
		}
	}

	bool ImageSizeSatisfiesCoverConstraints(const SIZE& imageSize) noexcept
	{
		constexpr LONG MinimumCoverDimension = 20;
		const auto width = static_cast<std::int64_t>(imageSize.cx);
		const auto height = static_cast<std::int64_t>(imageSize.cy);
		return width > MinimumCoverDimension && height > MinimumCoverDimension &&
			(height >= width || width < height * 2);
	}
}
