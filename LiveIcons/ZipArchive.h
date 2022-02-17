#pragma once
#include "Zip.h"
#include "ZipPosition.h"
#include "ZipCache.h"

namespace Zip
{
	using namespace std;

	class Archive final
	{
		wstring ZipName;
		unzFile ZipHandle;
		unique_ptr<Cache> ZipFiles;

	public:
		explicit Archive(wstring fileName) : ZipName(move(fileName)), ZipHandle(nullptr) { }
		~Archive();

		[[nodiscard]] int Open();
		[[nodiscard]] const Position* Find(const function<bool(const string&)>& pathMatch, size_t startIndex = 0) const;
		void SetCurrent(const Position& position) const { if (ZipFiles != nullptr) ZipFiles->SetCurrent(position); }

		[[nodiscard]] bool ReadMatching(string& outFilePath, vector<char>& content, const function<bool(const string&)>& pathMatch) const;
		[[nodiscard]] bool ReadCurrent(vector<char>& content) const;
		[[nodiscard]] bool ReadPath(const string& filePath, vector<char>& content) const;

		[[nodiscard]] bool FileExists(const string& filePath) const { return Find([&, filePath](const string& path) -> bool { return StrLib::EqualsCi(filePath, path); }) != END_OF_LIST; }
	};

	[[nodiscard]] wstring GetErrorMessage(int errorCode);
}