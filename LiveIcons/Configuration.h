#pragma once
#include "Registry.h"

#define CLSID_LIVE_ICONS_HANDLER_STR L"{434647DD-455C-4F43-AA12-6EFD055F5B08}"
#define LIVE_ICONS_HANDLER_NAME L"Live Icons Handler"
#define REG_SOFTWARE_CLASSES_CLSID L"Software\\Classes\\CLSID\\"
#define REG_INPROCSERVER32 L"\\InProcServer32"
#define REG_SOFTWARE_CLASSES L"Software\\Classes\\"
#define CLSID_EPUB_THUMBNAIL_PROVIDER_PATH L"Software\\Classes\\.epub\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}"
#define CLSID_FB2_THUMBNAIL_PROVIDER_PATH L"Software\\Classes\\.fb2\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}"

typedef HRESULT(*CreateInstanceFunction)(REFIID riid, void** ppvObject);

struct ClassInstantiator
{
	const CLSID* Clsid;
	CreateInstanceFunction CreateInstance;
};

class Configuration
{
public:
	constexpr static CLSID CLSID_LIVE_ICONS_HANDLER{ 0x434647dd, 0x455c, 0x4f43, { 0xaa, 0x12, 0x6e, 0xfd, 0x5, 0x5f, 0x5b, 0x8 } }; // {434647DD-455C-4F43-AA12-6EFD055F5B08}

	// ReSharper disable once CppVariableCanBeMadeConstexpr (No it can't!)
	const static std::vector<ClassInstantiator> CLASS_INSTANTIATORS;

	static CreateInstanceFunction GetInstantiatorFunction(const IID& clsid);
};
