#pragma once
#include "ParserBase.h"
#include "XmlDocument.h"
#include "chm_lib.h"

namespace Parser
{
	class Chm final : public Base
	{
		typedef struct IStreamReaderCtx { IStream* Stream; } IStreamReaderCtx;
		static vector<string> ImageFileExtensions;
		static vector<string> HtmlExtensions;
		static vector<string> TocFileNames;
		static vector<string> OtherTocFileNames;

		[[nodiscard]] Result Parse(const vector<char>& fileContent) const;
		bool TryGetCoverBitmap(IStream* stream, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromXsFile(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromToc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromHhc(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetPathFromHhcObjects(chm_file& chmFile, const Xml::Document& hhcXml, const function<bool(const string&)>& isParamTagIndicatesCoverObject, const function<bool(const string&)>& isParamTagWithCoverPath, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetPathFromHhcObject(chm_file& chmFile, const Xml::Document& hhcObjectXml, const function<bool(const string&)>& isParamTagIndicatesCoverObject, const function<bool(const string&)>& isParamTagWithCoverPath, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverByHtmlPath(chm_file& chmFile, const string& htmlFilePath, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryGetCoverFromImageTag(chm_file& chmFile, const Xml::Document& xml, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		void PreparePath(string& path) const;
		bool TryGetCoverBitmap(chm_file& chmFile, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType, const function<bool(const string&)>& pathMatch);
		bool TryGetFileContent(chm_file& chmFile, int& fileIndex, vector<char>& outFileContent, const function<bool(const string&)>& pathMatch);
		static bool TryLoadBitmap(const vector<char>& coverImage, HBITMAP& outBitmap, WTS_ALPHATYPE& outAlphaType);
		bool TryReadFile(chm_file& chmFile, chm_entry& fileEntry, vector<char>& outFileContent);
		static int64_t IStreamReader(void* ctxPtr, void* buffer, const int64_t offset, const int64_t size);

	public:
		bool CanParse(const wstring& fileExtension) override;
		Result Parse(const wstring& fileName) override;
		Result Parse(IStream* stream) override;
	};
}