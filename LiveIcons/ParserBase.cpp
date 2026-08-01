#include "pch.h"
#include "ParserBase.h"

namespace Parser
{
	bool Base::CanParse(const wstring&)
	{
		return false;
	}

	Result Base::Parse(const wstring&)
	{		
		return Result{ E_NOTIMPL };
	}

	Result Base::Parse(IStream*)
	{
		return Result{ E_NOTIMPL };
	}
}
