#pragma once
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Utility.h"

namespace Xml
{
	using namespace std;

	class Document
	{
		const string XmlStr;

	public:
		explicit Document(string xmlStr) : XmlStr(move(xmlStr)) { }
		[[nodiscard]] bool Empty() const { return XmlStr.empty(); }
		[[nodiscard]] const string& GetString() const { return XmlStr; }

		bool GetTag(const string& tagName, size_t searchStartOffset, size_t& outTagOffset, string& outTag) const;
		bool GetTag(const string& tagName, string& outTag, const function<bool(const string&)>& match) const;
		bool ContainsTag(const string& tagName, const function<bool(const string&)>& match) const;
		bool GetTagThatContains(const string& containsStr, string& outTag) const;
		bool GetTagThatContains(const string& tagName, const string& containsStr, string& outTag) const;
		bool GetTagAttributeValue(const string& tagName, const string& containsStr, const string& attributeName, string& outAttributeValue) const;
		bool GetElementContent(const string& elementName, const size_t offset, string& outElementContent, size_t* outContentOffset = nullptr) const;
		bool GetElementTagContainsContentPos(const string& elementName, const string& tagContainsStr, const size_t startOffset, span<char>& outContent, DataSpan&
		                                     outElementDataSpan) const;		
		static bool GetTagAttribute(const string& tag, const string& attributeName, string& outAttributeValue);

	private:
		[[nodiscard]] size_t FindTagStart(const string& findTagName, const size_t searchStartOffset, const bool isOpeningTag) const;
		size_t ReadTagName(size_t tagNameOffset, string& outTagName) const;
		static void RemoveNamespace(string& name);
		static bool ReadAttribute(const string& tag, size_t& outAttributeBeginOffset, string& outAttributeName, string& outAttributeValue);
	};
}