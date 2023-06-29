#pragma once
#include "ParserBase.h"

namespace Parser
{
	class Cbr final : public Base
	{
		Result Parse(const vector<char>& fileContent) const;
	public:
		bool CanParse(const wstring& fileExtension) override;
		Result Parse(const wstring& fileName) override;
		Result Parse(IStream* stream) override;
	};
}

