#include <iostream>
#include <format>
#include <chrono>
#include <iostream>
#include "../LiveIcons/StrLib.h"
#include "../LiveIcons/Utility.h"
#include "../LiveIcons/LiveIcons.h"

using namespace std;

void GetThumbnails(const wstring& readPath, const wstring& writePath);
Parser::Result GetThumbnail(const wstring& filePath);
void FindFiles(std::vector<wstring>& files, const std::wstring& path);
wstring GetWriteFileName(const wstring& sourceFilePath, const wstring& writePath, const wstring& extension);
wstring GetFileName(const wstring& filePath);
void WriteLog(const wstring& message);
struct GroupThousands final : numpunct<char> { [[nodiscard]] basic_string<char> do_grouping() const override { return "\3"; } };

int wmain(int argc, wchar_t* argv[])
{
	if (argc != 3)
	{
		wcout << L"Usage: Testing.exe \"C:\\Read\\Path\\\" \"C:\\Write\\Path\\\"" << endl;
		return 0;
	}

	const auto readPath = wstring{ argv[1] };
	const auto writePath = wstring{ argv[2] };

	// Enable the console to display cyrillic characters
	SetConsoleOutputCP(1251);
	SetConsoleCP(1251);
	setlocale(LC_ALL, "Russian");
	wcout.imbue(std::locale(std::wcout.getloc(), new GroupThousands{}));

	GetThumbnails(readPath, writePath);
}

void GetThumbnails(const wstring& readPath, const wstring& writePath)
{
	auto files = vector<wstring>{};
	FindFiles(files, readPath);

	auto fileContent = vector<char>{};
	for (const wstring& filePath : files)
	{
		const auto&& result = GetThumbnail(filePath);
		if (FAILED(result.HResult))
		{
			WriteLog(std::format(L"Failure: '{}'; Message: '{}'", filePath, result.Error));
			continue;
		}
		
		if (result.Cover == nullptr) 
			continue; // Unsupported extension

		if (SaveImage(result.Cover, GetWriteFileName(filePath, writePath, L".png"), Gfx::ImageFileType::Png))
			WriteLog(std::format(L"Success: '{}'", filePath));
		else
			WriteLog(std::format(L"Failure: '{}'; Message: 'Parsing success, failed to save'", filePath));
	}
}

void FindFiles(std::vector<wstring>& files, const std::wstring& path)
{
	for (const auto& file : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied))
	{
		const auto filePath = file.path();
		if (file.is_directory())
		{
			FindFiles(files, filePath);
			continue;
		}

		files.push_back(wstring{ filePath });
	}
}

Parser::Result GetThumbnail(const wstring &filePath)
{
	try
	{
		std::wstring fileExtension{};
		if (const auto result = Utility::GetFileExtension(filePath, fileExtension); FAILED(result))
			return Parser::Result{ result };

		for (const auto parsers = LiveIcons::GetParsers(); const auto& parser : parsers)
		{
			if (parser->CanParse(fileExtension))
				return parser->Parse(filePath);
		}

		return Parser::Result{ S_OK };
	}
	catch (const std::exception& ex)
	{
		const auto message = ex.what();
		return Parser::Result{ E_FAIL, StrLib::ToWstring(message) };
	}
}

wstring GetWriteFileName(const wstring& sourceFilePath, const wstring& writePath, const wstring& extension)
{
	auto fileName = GetFileName(sourceFilePath);
	if (fileName.empty())
		return L"";
	fileName += fileName.front() == L'.'
		? extension
		: L'.' + extension;
	return !writePath.empty()
		? writePath.back() == L'\\'
			? writePath + fileName
			: writePath + L"\\" + fileName
		: fileName;
}

wstring GetFileName(const wstring& filePath)
{
	auto offset = filePath.find_last_of(L'\\');
	if (offset == wstring::npos)
		return filePath;
	offset++;
	return offset < filePath.size()
		? filePath.substr(offset)
		: L"";
}

void WriteLog(const wstring& message)
{
	wcout << format(L"{:%Y-%m-%d %T} {}", static_cast<chrono::sys_time<chrono::nanoseconds>>(chrono::system_clock::now()), message) << endl;
}