#include "pch.h"
#include "ParserBase.h"
#include "Utility.h"

namespace Parser
{
	bool Base::CanParse(const wstring& fileName)
	{
		Log::Write("Parser::Base: Error: Invalid Base Method Call.");
		return false;
	}

	Result Base::Parse(const wstring& fileName)
	{		
		return Result{ E_NOTIMPL };
	}

	Result Base::Parse(IStream* stream)
	{
		return Result{ E_NOTIMPL };
	}
}