#pragma once
#include <memory>
#include <string>
#include <Windows.h>
#include <system_error>
#include <thumbcache.h>

#include "StrLib.h"

namespace Parser
{
	using namespace std;

	struct Result
	{
		HRESULT HResult;
		wstring Error{};
		wstring Title{};
		HBITMAP Cover;
		WTS_ALPHATYPE CoverAlpha;

		Result() noexcept : HResult{ S_OK }, Cover{ nullptr }, CoverAlpha{ WTSAT_UNKNOWN } { }
		explicit Result(const HRESULT hResult) noexcept : HResult{ hResult }, Error{ StrLib::ToWstring(system_category().message(hResult)) }, Cover{nullptr}, CoverAlpha{WTSAT_UNKNOWN} { }
		explicit Result(const HRESULT hResult, wstring error) noexcept : HResult{ hResult }, Error{ move(error) }, Cover{ nullptr }, CoverAlpha{ WTSAT_UNKNOWN } { }
		explicit Result(wstring title, const HBITMAP cover, const WTS_ALPHATYPE coverAlpha) noexcept : HResult{ S_OK }, Title{ move(title) }, Cover{ cover }, CoverAlpha{ coverAlpha } { }
		Result(const Result& other) = default;
		Result(Result&& other) noexcept : HResult(other.HResult), Title(std::move(other.Title)), Cover(other.Cover), CoverAlpha(other.CoverAlpha) { }
		Result& operator=(const Result& other) {
			if (this == &other)
				return *this;
			HResult = other.HResult;
			Title = other.Title;
			Cover = other.Cover;
			CoverAlpha = other.CoverAlpha;
			return *this;
		}
		Result& operator=(Result&& other) noexcept
		{
			if (this == &other)
				return *this;
			HResult = other.HResult;
			Title = std::move(other.Title);
			Cover = other.Cover;
			CoverAlpha = other.CoverAlpha;
			return *this;
		}
		~Result() = default;
	};

	class Base
	{
	public:
		virtual bool CanParse(const wstring& fileExtension);
		virtual Result Parse(const wstring& fileName);			// Parse using the file name (Testing)
		virtual Result Parse(IStream* stream);					// Parse Windows file bytes stream
		virtual ~Base() = default;
	};
}
