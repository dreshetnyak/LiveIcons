#pragma once
#include "ParserBase.h"
#include "Utility.h"
#include "Gfx.h"
#include "XmlDocument.h"
#include "../UnRar/dll.hpp"
#include "../UnRar/dll.cpp"
#include "../UnRar/file.hpp"
#include "../UnRar/archive.hpp"

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

