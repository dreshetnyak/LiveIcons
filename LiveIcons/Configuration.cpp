#include "pch.h"
#include "Configuration.h"
#include "LiveIcons.h"

const std::vector<ClassInstantiator> Configuration::CLASS_INSTANTIATORS
{
	ClassInstantiator{&CLSID_LIVE_ICONS_HANDLER, LiveIcons::CreateInstance}
};

CreateInstanceFunction Configuration::GetInstantiatorFunction(const IID& clsid)
{
	const auto instantiators = CLASS_INSTANTIATORS;
	for (auto instantiator = instantiators.begin(); instantiator != instantiators.end(); ++instantiator)
	{
		if (clsid == *instantiator->Clsid)
			return *instantiator->CreateInstance;
	}
	
	return nullptr;
}