// Testing.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <codecvt>
#include <iostream>
#include <ioapi.h>
#include <iowin32.h>
#include <locale>
#include <unzip.h>
#include <filesystem>
#include <string>
#include <chrono>
#include <fcntl.h>

#include "parser_epub.h"
#include "stopwatch.h"
#include "utility.h"
#include "xml_document.h"
#include <corecrt_io.h>

// case sensitivity when searching for filenames
#define CASE_SENSITIVE 1
#define CASE_INSENSITIVE 2

using namespace std;

struct item_counter
{
	string item;
	int count;
};

void find_files(std::vector<wstring>& files, const std::wstring& path);
void init_locale();

int main()
{
	//const auto path1 = std::wstring{ L"R:\\Books" };
	//const auto path2 = std::wstring{ L"R:\\pCloud\\Books" };
	//const auto path3 = std::wstring{ L"R:\\Temp\\EPUB3" };
	//const auto path4 = std::wstring{ L"R:\\Temp\\EPUB" };
	const auto path5 = std::wstring{ L"R:\\Temp\\EPUBZ" };

	SetConsoleOutputCP(1251);
	SetConsoleCP(1251);
	setlocale(LC_ALL, "Russian");
	wcout.imbue(std::locale(std::wcout.getloc(), new strlib::group_thousands));

	std::vector<wstring> files;
	//find_files(files, path1);
	//find_files(files, path2);
	//find_files(files, path3);
	//find_files(files, path4);
	find_files(files, path5);

	vector<item_counter> file_extension_counts;
	vector<item_counter> root_file_paths_counts;

	int total_count = 0;
	int success_count = 0;
	int failure_count = 0;

	parser::epub parser;

	const auto sw_total = utility::stopwatch::start_new();

	for (const wstring& file_path : files)
	{		
		wcout << format(L"\r[Parsing]: '{}'", file_path);
		if (wcout.fail()) { wcout.clear(); wcout << endl; }
		
		const auto sw_parse = utility::stopwatch::start_new();
		const auto result = parser.parse(file_path);
		const auto parse_ms = sw_parse.elapsed<chrono::milliseconds>();
		
		if (result->error.empty())
		{
			++success_count;
			wcout << format(L"\r[Success]: '{}'; Title: '{}'; Time: {} ms.", file_path, result->title, parse_ms) << endl;
			if (wcout.fail()) { wcout.clear(); wcout << endl; }
		}
		else
		{
			++failure_count;
			wcout << format(L"\r[Failure]: '{}'; Title: '{}'; Error: '{}'; Time: {} ms.", file_path, result->title, result->error, parse_ms) << endl;
			if (wcout.fail()) { wcout.clear(); wcout << endl; }
		}

		++total_count;
	}

	const auto total_ms = sw_total.elapsed<std::chrono::milliseconds>();

	wcout << endl << "Total  : " << total_count << endl;
	wcout << "Success: " << success_count << endl;
	wcout << "Failure: " << failure_count << endl;
	wcout << "Elapsed: " << total_ms << endl;
}

void find_files(std::vector<wstring>& files, const std::wstring& path)
{
	for (const auto& file : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied))
	{
		const auto file_path = file.path();
		if (file.is_directory())
		{
			find_files(files, file_path);
			continue;
		}

		if (auto extension = static_cast<wstring>(file_path.extension()); strlib::equals_ci<wchar_t>(extension, L".epub"))
			files.push_back(wstring{ file_path });
	}
}

void init_locale()
{
//#if MS_STDLIB_BUGS
	//constexpr char cp_utf16le[] = ".1200";
	//setlocale(LC_ALL, cp_utf16le);
	//_setmode(_fileno(stdout), _O_WTEXT);
//#else
	// The correct locale name may vary by OS, e.g., "en_US.utf8".
	constexpr char locale_name[] = "";
	setlocale(LC_ALL, locale_name);
	std::locale::global(std::locale(locale_name));
	std::wcin.imbue(std::locale());
	std::wcout.imbue(std::locale());
//#endif
}
