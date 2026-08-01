#include "pch.h"
#include "Configuration.h"
#include "LiveIcons.h"

#include <array>

namespace
{
	constexpr std::array ClassInstantiators
	{
		ClassInstantiator{ &Configuration::CLSID_LIVE_ICONS_HANDLER, LiveIcons::CreateInstance }
	};
}

CreateInstanceFunction Configuration::GetInstantiatorFunction(const IID& clsid) noexcept
{
	for (const auto& instantiator : ClassInstantiators)
	{
		if (clsid == *instantiator.Clsid)
			return instantiator.CreateInstance;
	}
	
	return nullptr;
}
