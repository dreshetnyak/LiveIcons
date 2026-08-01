#pragma once
#include <memory>
#include <string>
#include <utility>
#include <Windows.h>
#include <thumbcache.h>

#include "StrLib.h"

namespace Parser
{
	using namespace std;

	struct Result
	{
		HRESULT HResult{ S_OK };
		wstring Error{};
		wstring Title{};
		WTS_ALPHATYPE CoverAlpha{ WTSAT_UNKNOWN };

		Result() noexcept = default;
		explicit Result(const HRESULT hResult) noexcept : HResult{ hResult } { }
		explicit Result(const HRESULT hResult, wstring error) noexcept :
			HResult{ hResult }, Error{ move(error) }
		{
		}
		explicit Result(wstring title, const HBITMAP cover, const WTS_ALPHATYPE coverAlpha) noexcept :
			Title{ move(title) }, Cover{ cover }, CoverAlpha{ coverAlpha }
		{
		}

		Result(const Result&) = delete;
		Result& operator=(const Result&) = delete;

		Result(Result&& other) noexcept :
			HResult{ other.HResult },
			Error{ std::move(other.Error) },
			Title{ std::move(other.Title) },
			Cover{ std::exchange(other.Cover, nullptr) },
			CoverAlpha{ other.CoverAlpha }
		{
		}

		Result& operator=(Result&& other) noexcept
		{
			if (this == &other)
				return *this;

			ResetCover();
			HResult = other.HResult;
			Error = std::move(other.Error);
			Title = std::move(other.Title);
			Cover = std::exchange(other.Cover, nullptr);
			CoverAlpha = other.CoverAlpha;
			return *this;
		}

		~Result() noexcept
		{
			ResetCover();
		}

		[[nodiscard]] HBITMAP GetCover() const noexcept
		{
			return Cover;
		}

		[[nodiscard]] HBITMAP ReleaseCover() noexcept
		{
			return std::exchange(Cover, nullptr);
		}

	private:
		HBITMAP Cover{};

		void ResetCover() noexcept
		{
			if (Cover != nullptr)
			{
				DeleteObject(Cover);
				Cover = nullptr;
			}
		}
	};

	class Base
	{
	public:
		virtual bool CanParse(const wstring& fileExtension);
		virtual Result Parse(const wstring& fileName);
		virtual Result Parse(IStream* stream);
		virtual ~Base() = default;
	};
}
