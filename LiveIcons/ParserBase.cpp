#include "pch.h"
#include "ParserBase.h"

namespace Parser
{
	bool Base::CanParse(const wstring& fileExtension)
	{
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