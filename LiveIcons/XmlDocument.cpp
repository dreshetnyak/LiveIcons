#include "pch.h"
#include "XmlDocument.h"
#include "Utility.h"

namespace Xml
{
	bool Document::GetTag(const string& tagName, const size_t searchStartOffset, size_t& outTagOffset, string& outTag) const
	{
		const auto tagBegin = FindTagStart(tagName, searchStartOffset, true);
		if (tagBegin == string::npos)
			return false;
		const auto tagEnd = XmlStr.find('>', tagBegin);
		if (tagEnd == string::npos)
			return false;
		outTagOffset = tagBegin;
		outTag = XmlStr.substr(tagBegin, tagEnd - tagBegin + 1);
		return true;
	}

	bool Document::GetTag(const string& tagName, string& outTag, const function<bool(const string&)>& match) const
	{
		for (size_t tagSearchOffset = 0, tagOffset; GetTag(tagName, tagSearchOffset, tagOffset, outTag); tagSearchOffset += outTag.size())
		{
			if (match(outTag))
				return true;
		}

		return false;
	}

	bool Document::ContainsTag(const string& tagName, const function<bool(const string&)>& match) const
	{
		string tag{};
		for (size_t tagSearchOffset = 0, tagOffset; GetTag(tagName, tagSearchOffset, tagOffset, tag); tagSearchOffset += tag.size())
		{
			if (match(tag))
				return true;
		}

		return false;
	}

	bool Document::GetTagThatContains(const string& tagName, const string& containsStr, string& outTag) const
	{
		size_t containedStrOffset = 0;
		while ((containedStrOffset = StrLib::FindCi(XmlStr, containsStr, containedStrOffset)) != string::npos)
		{
			const auto tagBegin = StrLib::FindReverse(XmlStr, '<', containedStrOffset++);
			if (tagBegin == string::npos)
				continue;

			string currentTagName;
			if (const auto tagEndOffset = ReadTagName(tagBegin + 1, currentTagName);
				tagEndOffset == string::npos ||
				!StrLib::EqualsCi(currentTagName, tagName))
				continue;

			const auto tagEnd = XmlStr.find('>', containedStrOffset);
			if (tagEnd == string::npos)
				return false;

			outTag = XmlStr.substr(tagBegin, tagEnd - tagBegin + 1);
			return true;
		}

		return false;
	}

	bool Document::GetTagThatContains(const string& containsStr, string& outTag) const
	{
		const auto containedStrBegin = StrLib::FindCi(XmlStr, containsStr, 0);
		if (containedStrBegin == string::npos)
			return false;

		const auto tagBegin = StrLib::FindReverse(XmlStr, '<', containedStrBegin);
		if (tagBegin == string::npos)
			return false;

		const auto tagEnd = XmlStr.find('>', containedStrBegin);
		if (tagEnd == string::npos)
			return false;

		outTag = XmlStr.substr(tagBegin, tagEnd - tagBegin + 1);
		return true;
	}

	bool Document::GetElementContent(const string& elementName, const size_t offset, string& outElementContent, size_t* outContentOffset) const
	{
		const auto openingTagBegin = FindTagStart(elementName, offset, true);
		if (openingTagBegin == string::npos)
			return false;
		const auto openingTagEnd = XmlStr.find_first_of('>', openingTagBegin);
		if (openingTagEnd == string::npos)
			return false;
		const auto xmlStrSize = XmlStr.size();
		const auto contentBegin = openingTagEnd + 1;
		if (contentBegin >= xmlStrSize)
			return false;

		const auto closingTagBegin = FindTagStart(elementName, contentBegin, false);
		if (closingTagBegin == string::npos)
			return false;

		outElementContent = XmlStr.substr(contentBegin, closingTagBegin - contentBegin);
		if (outContentOffset != nullptr)
			*outContentOffset = contentBegin;

		return true;
	}

	bool Document::GetElementTagContainsContentPos(const string& elementName, const string& tagContainsStr, const size_t startOffset, span<char>& outContent,  DataSpan& outElementDataSpan) const
	{
		size_t containedStrOffset = startOffset;
		while ((containedStrOffset = StrLib::FindCi(XmlStr, tagContainsStr, containedStrOffset)) != string::npos)
		{
			const auto tagBegin = StrLib::FindReverse(XmlStr, '<', containedStrOffset++);
			if (tagBegin == string::npos)
				continue;

			string currentTagName;
			if (const auto tagEndOffset = ReadTagName(tagBegin + 1, currentTagName);
				tagEndOffset == string::npos ||
				!StrLib::EqualsCi(currentTagName, elementName))
				continue;

			const auto tagEnd = XmlStr.find('>', containedStrOffset);
			if (tagEnd == string::npos)
				return false;

			const auto closingTag = format("</{}>", elementName);
			const auto closingTagOffset = StrLib::FindCi(XmlStr, closingTag, tagEnd);
			if (closingTagOffset == string::npos)
				return false;

			const auto contentSize = closingTagOffset - tagEnd - 1;
			if (contentSize == 0)
				return false;

			outContent = span{ const_cast<char*>(XmlStr.data() + tagEnd + 1), contentSize};
			outElementDataSpan.Offset = tagBegin;
			outElementDataSpan.Size = (closingTagOffset + closingTag.size()) - tagBegin;
			return true;
		}

		return false;
	}

	//tag must be "<tag attr="" />"
	bool Document::GetTagAttribute(const string& tag, const string& attributeName, string& outAttributeValue)
	{
		auto attributeBeginOffset = tag.find_first_of(" \r\n\t");
		if (attributeBeginOffset == string::npos)
			return false;
		attributeBeginOffset = tag.find_first_not_of(" \r\n\t", attributeBeginOffset);
		if (attributeBeginOffset == string::npos)
			return false;

		string readAttributeName;
		string readAttributeValue;
		while (ReadAttribute(tag, attributeBeginOffset, readAttributeName, readAttributeValue))
		{
			if (!StrLib::EqualsCi(readAttributeName, attributeName))
				continue;
			outAttributeValue = readAttributeValue;
			return true;
		}

		return false;
	}

	bool Document::GetTagAttributeValue(const string& tagName, const string& containsStr, const string& attributeName, string& outAttributeValue) const
	{
		string tag;
		return GetTagThatContains(tagName, containsStr, tag) &&
			GetTagAttribute(tag, attributeName, outAttributeValue);
	}

	bool Document::ReadAttribute(const string& tag, size_t& outAttributeBeginOffset, string& outAttributeName, string& outAttributeValue)
	{
		if (outAttributeBeginOffset = tag.find_first_not_of(" \r\n\t", outAttributeBeginOffset); outAttributeBeginOffset == string::npos)
			return false;
		const auto attributeNameEnd = tag.find('=', outAttributeBeginOffset);
		if (attributeNameEnd == string::npos)
			return false;
		outAttributeName = tag.substr(outAttributeBeginOffset, attributeNameEnd - outAttributeBeginOffset);
		if (outAttributeName.find_first_of(" \'\"<>=") != string::npos) // Make sure that the name does not have those characters
			return false;
		RemoveNamespace(outAttributeName);
		const auto tagSize = tag.size();
		auto attributeValueBegin = attributeNameEnd + 1;
		if (attributeValueBegin >= tagSize)
			return false;
		const auto attributeValueQuote = tag[attributeValueBegin];
		const auto hasQuote = attributeValueQuote == '\'' || attributeValueQuote == '\"';
		if (hasQuote)
			attributeValueBegin++; //Skip quote
		if (attributeValueBegin >= tagSize)
			return false;
		const auto attributeValueEnd = hasQuote
			? tag.find(attributeValueQuote, attributeValueBegin)
			: tag.find_first_of(" />", attributeValueBegin);
		if (attributeValueEnd == string::npos)
			return false;
		outAttributeValue = tag.substr(attributeValueBegin, attributeValueEnd - attributeValueBegin);
		outAttributeBeginOffset = hasQuote ? attributeValueEnd + 1 : attributeValueEnd;
		return true;
	}

	size_t Document::FindTagStart(const string& findTagName, const size_t searchStartOffset, const bool isOpeningTag) const
	{
		const auto xmlStrSize = XmlStr.size();
		const auto tagNameSize = findTagName.size();
		if (tagNameSize > xmlStrSize - searchStartOffset)
			return string::npos;

		const auto tagNameOffset = isOpeningTag ? 1 : 2;
		const auto indexLimit = xmlStrSize - tagNameSize;
		for (auto offset = searchStartOffset; offset < indexLimit; ++offset)
		{
			offset = isOpeningTag
				? XmlStr.find('<', offset)
				: XmlStr.find("</", offset);
			if (offset == string::npos)
				break;
			string tagName;
			const auto tagEndOffset = ReadTagName(offset + tagNameOffset, tagName);
			if (tagEndOffset == string::npos)
				break;
			if (StrLib::EqualsCi(tagName, findTagName))
				return offset;
			offset = tagEndOffset;
		}

		return string::npos;
	}

	size_t Document::ReadTagName(const size_t tagNameOffset, string& outTagName) const
	{
		if (tagNameOffset >= XmlStr.size())
			return string::npos;
		const auto tagEnd = XmlStr.find_first_of(" />", tagNameOffset);
		if (tagEnd == string::npos)
			return string::npos;
		outTagName = XmlStr.substr(tagNameOffset, tagEnd - tagNameOffset);
		RemoveNamespace(outTagName);
		return tagEnd;
	}

	void Document::RemoveNamespace(string& name)
	{
		if (const auto divisorOffset = name.find(':'); divisorOffset != string::npos)
			name.erase(0, divisorOffset + 1);
	}
}