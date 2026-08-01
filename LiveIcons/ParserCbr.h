#pragma once

#include "ParserBase.h"

namespace Parser
{
	class Cbr final : public Base
	{
	public:
		bool CanParse(const std::wstring& fileExtension) override;
		Result Parse(const std::wstring& fileName) override;
		Result Parse(IStream* stream) override;
	};
}
