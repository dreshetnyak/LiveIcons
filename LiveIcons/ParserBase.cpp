#include "pch.h"
#include "ParserBase.h"

namespace Parser
{
	shared_ptr<Result> Base::Parse(const wstring& fileName)
	{
		return shared_ptr<Result>{ new Result{ L"Not implemented", {}, 0, {}, nullptr} };
	}

	shared_ptr<Result> Base::Parse(IStream* stream)
	{
		return shared_ptr<Result>{ new Result{ L"Not implemented", {}, 0, {}, nullptr } };
	}
}
