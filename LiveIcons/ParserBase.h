#pragma once
#include <memory>
#include <string>
#include <Windows.h>

namespace Parser
{
	using namespace std;

	struct Result
	{
		wstring Error;
		wstring Title;
		size_t Size;
		wstring CoverPath;
		HBITMAP* Thumbnail;
	};

	class Base
	{
	public:
		virtual bool CanParse(const wstring& fileName) = 0;
		virtual shared_ptr<Result> Parse(const wstring& fileName);
		virtual shared_ptr<Result> Parse(IStream* stream);
		virtual ~Base() = default;
	};
}