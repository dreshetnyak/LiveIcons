#pragma once
#include <span>
#include <string_view>

#include "ParserBase.h"
#include "XmlDocument.h"
#include "chm_lib.h"

namespace Parser
{
	class Chm final : public Base
	{
		struct IStreamReaderCtx final
		{
			IStream* Stream;
			ULONGLONG Size;
		};

		[[nodiscard]] Result Parse(const vector<char>& fileContent) const;
		bool TryGetCoverBitmap(IStream* stream, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromXsFile(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromToc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromHhc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetPathFromHhcObjects(chm_file& chmFile, const Xml::Document& hhcXml, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetPathFromHhcObject(chm_file& chmFile, const Xml::Document& hhcObjectXml, const function<bool(const string&)>& isParamTagIndicatesCoverObject, const function<bool(const string&)>& isParamTagWithCoverPath, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverByFileName(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromFirstHtml(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverByHtmlPath(chm_file& chmFile, const string& htmlFilePath, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromImageTag(chm_file& chmFile, const Xml::Document& xml, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		void PreparePath(string& path) const;
		bool TryGetCoverBitmap(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType, const function<bool(const string&)>& pathMatch);
		bool TryGetCoverFromHtml(chm_file& chmFile, std::span<const std::string_view> endsWithStrings, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetFileMatchContent(chm_file& chmFile, int& fileIndex, vector<char>& outFileContent, const function<bool(const string&)>& pathMatch);
		bool TryGetFileEndsWithContent(chm_file& chmFile, int& fileIndex, vector<char>& outFileContent, std::string_view endsWith);
		static bool TryLoadBitmap(const vector<char>& coverImage, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryReadFile(chm_file& chmFile, chm_entry& fileEntry, vector<char>& outFileContent);
		static int64_t IStreamReader(void* ctxPtr, void* buffer, int64_t offset, int64_t size) noexcept;

	public:
		bool CanParse(const wstring& fileExtension) override;
		Result Parse(const wstring& fileName) override;
		Result Parse(IStream* stream) override;
	};
}
