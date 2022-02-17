#pragma once
#include <string>
#include <utility>
#include <vector>

namespace Xml
{
	using namespace std;

	class Document
	{
		const string XmlStr;

	public:
		explicit Document(string xmlStr) : XmlStr(move(xmlStr)) { }
		[[nodiscard]] bool Empty() const { return XmlStr.empty(); }
		bool GetTag(const string& tagName, const size_t searchStartOffset, string& outTag) const;
		bool GetTagThatContains(const string& containsStr, string& outTag) const;
		bool GetTagThatContains(const string& tagName, const string& containsStr, string& outTag) const;
		bool GetTagAttributeValue(const string& tagName, const string& containsStr, const string& attributeName, string& outAttributeValue) const;
		bool GetElementContent(const string& elementName, const size_t offset, string& outElementContent) const;
		static bool GetTagAttribute(const string& tag, const string& attributeName, string& outAttributeValue);

	private:
		[[nodiscard]] size_t FindTagStart(const string& findTagName, const size_t searchStartOffset, const bool isOpeningTag) const;
		size_t ReadTagName(size_t tagNameOffset, string& outTagName) const;
		static void RemoveNamespace(string& name);
		static bool ReadAttribute(const string& tag, size_t& outAttributeBeginOffset, string& outAttributeName, string& outAttributeValue);
	};
}