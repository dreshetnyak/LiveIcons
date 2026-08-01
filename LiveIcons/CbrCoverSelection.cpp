#include "pch.h"
#include "CbrCoverSelection.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string>

#include <xmllite.h>

#include "ComPtr.h"
#include "DataIStream.h"

namespace
{
	constexpr std::size_t MaximumComicInfoPageTags{ 10000 };
	constexpr LONG_PTR MaximumXmlElementDepth{ 64 };
	constexpr LONG_PTR MaximumXmlEntityExpansion{ 1024 * 1024 };

	[[nodiscard]] wchar_t NormalizePathCharacter(const wchar_t character) noexcept
	{
		return character == L'\\' ? L'/' : character;
	}

	[[nodiscard]] bool HasCharacterType(
		const wchar_t character, const WORD characterType) noexcept
	{
		WORD type{};
		return GetStringTypeW(CT_CTYPE1, &character, 1, &type) != FALSE &&
			(type & characterType) != 0;
	}

	[[nodiscard]] bool IsDigit(const wchar_t character) noexcept
	{
		if (character >= L'0' && character <= L'9')
			return true;
		if (character <= 0x7f)
			return false;
		return HasCharacterType(character, C1_DIGIT);
	}

	[[nodiscard]] bool IsLetter(const wchar_t character) noexcept
	{
		if ((character >= L'a' && character <= L'z') ||
			(character >= L'A' && character <= L'Z'))
			return true;
		if (character <= 0x7f)
			return false;
		return HasCharacterType(character, C1_ALPHA);
	}

	[[nodiscard]] int ComparePathCharacters(
		const wchar_t rawLeft, const wchar_t rawRight) noexcept
	{
		const wchar_t left = NormalizePathCharacter(rawLeft);
		const wchar_t right = NormalizePathCharacter(rawRight);
		if (left == right)
			return 0;
		if (left <= 0x7f && right <= 0x7f)
		{
			const wchar_t foldedLeft = left >= L'A' && left <= L'Z'
				? left + (L'a' - L'A')
				: left;
			const wchar_t foldedRight = right >= L'A' && right <= L'Z'
				? right + (L'a' - L'A')
				: right;
			if (foldedLeft == foldedRight)
				return 0;
			return foldedLeft < foldedRight ? -1 : 1;
		}

		const int comparison = CompareStringOrdinal(&left, 1, &right, 1, TRUE);
		if (comparison == CSTR_LESS_THAN)
			return -1;
		if (comparison == CSTR_GREATER_THAN)
			return 1;
		if (comparison == CSTR_EQUAL)
			return 0;
		return left < right ? -1 : 1;
	}

	[[nodiscard]] int CompareNatural(
		const std::wstring_view left, const std::wstring_view right) noexcept
	{
		if (left.empty() || right.empty())
		{
			if (left.empty() == right.empty())
				return 0;
			return left.empty() ? -1 : 1;
		}

		const wchar_t firstLeft = NormalizePathCharacter(left.front());
		const wchar_t firstRight = NormalizePathCharacter(right.front());
		const bool leftStartsWithLetterOrDigit = IsLetter(firstLeft) || IsDigit(firstLeft);
		const bool rightStartsWithLetterOrDigit = IsLetter(firstRight) || IsDigit(firstRight);
		if (leftStartsWithLetterOrDigit != rightStartsWithLetterOrDigit)
			return leftStartsWithLetterOrDigit ? 1 : -1;

		std::size_t leftOffset{};
		std::size_t rightOffset{};
		while (leftOffset < left.size() && rightOffset < right.size())
		{
			const wchar_t leftCharacter = NormalizePathCharacter(left[leftOffset]);
			const wchar_t rightCharacter = NormalizePathCharacter(right[rightOffset]);
			const bool leftIsDigit = IsDigit(leftCharacter);
			const bool rightIsDigit = IsDigit(rightCharacter);
			if (leftIsDigit && rightIsDigit)
			{
				const std::size_t leftRunBegin = leftOffset;
				const std::size_t rightRunBegin = rightOffset;
				while (leftOffset < left.size() &&
					IsDigit(NormalizePathCharacter(left[leftOffset])))
					++leftOffset;
				while (rightOffset < right.size() &&
					IsDigit(NormalizePathCharacter(right[rightOffset])))
					++rightOffset;

				std::size_t leftSignificant = leftRunBegin;
				std::size_t rightSignificant = rightRunBegin;
				while (leftSignificant < leftOffset && left[leftSignificant] == L'0')
					++leftSignificant;
				while (rightSignificant < rightOffset && right[rightSignificant] == L'0')
					++rightSignificant;

				const std::size_t leftDigits = leftOffset - leftSignificant;
				const std::size_t rightDigits = rightOffset - rightSignificant;
				if (leftDigits != rightDigits)
					return leftDigits < rightDigits ? -1 : 1;

				for (std::size_t digit{}; digit < leftDigits; ++digit)
				{
					const wchar_t leftDigit = left[leftSignificant + digit];
					const wchar_t rightDigit = right[rightSignificant + digit];
					if (leftDigit != rightDigit)
						return leftDigit < rightDigit ? -1 : 1;
				}

				const std::size_t leftRunLength = leftOffset - leftRunBegin;
				const std::size_t rightRunLength = rightOffset - rightRunBegin;
				if (leftRunLength != rightRunLength)
				{
					// ComicRack uses the longer, more-zero-padded spelling first when
					// two numeric runs have the same value.
					return leftRunLength > rightRunLength ? -1 : 1;
				}
				continue;
			}

			if (leftIsDigit != rightIsDigit)
				return leftIsDigit ? -1 : 1;

			const bool leftIsLetter = IsLetter(leftCharacter);
			const bool rightIsLetter = IsLetter(rightCharacter);
			if (leftIsLetter != rightIsLetter)
				return leftIsLetter ? 1 : -1;

			if (const int comparison = ComparePathCharacters(
				leftCharacter, rightCharacter); comparison != 0)
				return comparison;
			++leftOffset;
			++rightOffset;
		}

		if (leftOffset == left.size() && rightOffset == right.size())
			return 0;
		return leftOffset == left.size() ? -1 : 1;
	}

	[[nodiscard]] wchar_t FoldAsciiCharacter(wchar_t character) noexcept
	{
		character = NormalizePathCharacter(character);
		if (character >= L'A' && character <= L'Z')
			character += L'a' - L'A';
		return character;
	}

	[[nodiscard]] bool IsCoverNameSeparator(const wchar_t character) noexcept
	{
		return character == L' ' || character == L'-' || character == L'_' ||
			character == L'.';
	}

	[[nodiscard]] bool EqualsNormalizedStem(
		const std::wstring_view stem, const std::wstring_view expected) noexcept
	{
		std::size_t stemOffset{};
		std::size_t expectedOffset{};
		while (stemOffset < stem.size() || expectedOffset < expected.size())
		{
			while (stemOffset < stem.size() && IsCoverNameSeparator(stem[stemOffset]))
				++stemOffset;
			if (stemOffset == stem.size() || expectedOffset == expected.size())
				return stemOffset == stem.size() && expectedOffset == expected.size();
			if (FoldAsciiCharacter(stem[stemOffset]) != expected[expectedOffset])
				return false;
			++stemOffset;
			++expectedOffset;
		}
		return true;
	}

	[[nodiscard]] bool EqualsAsciiCaseInsensitive(
		const std::wstring_view left, const std::wstring_view right) noexcept
	{
		if (left.size() != right.size())
			return false;
		for (std::size_t index{}; index < left.size(); ++index)
		{
			if (FoldAsciiCharacter(left[index]) != FoldAsciiCharacter(right[index]))
				return false;
		}
		return true;
	}

	[[nodiscard]] bool IsTypeDelimiter(const wchar_t character) noexcept
	{
		return character == L',' || character == L' ' || character == L'\t' ||
			character == L'\r' || character == L'\n' || character == L'\v' ||
			character == L'\f';
	}

	[[nodiscard]] bool HasFrontCoverToken(const std::wstring_view types) noexcept
	{
		static constexpr std::wstring_view FrontCover{ L"frontcover" };
		std::size_t offset{};
		while (offset < types.size())
		{
			while (offset < types.size() && IsTypeDelimiter(types[offset]))
				++offset;
			const std::size_t tokenBegin = offset;
			while (offset < types.size() && !IsTypeDelimiter(types[offset]))
				++offset;
			if (EqualsAsciiCaseInsensitive(
				types.substr(tokenBegin, offset - tokenBegin), FrontCover))
				return true;
		}
		return false;
	}

	[[nodiscard]] bool IsXmlWhitespace(const wchar_t character) noexcept
	{
		return character == L' ' || character == L'\t' ||
			character == L'\r' || character == L'\n';
	}

	[[nodiscard]] bool TryParseComicInfoIndex(
		std::wstring_view value, std::size_t& outIndex) noexcept
	{
		while (!value.empty() && IsXmlWhitespace(value.front()))
			value.remove_prefix(1);
		while (!value.empty() && IsXmlWhitespace(value.back()))
			value.remove_suffix(1);
		if (!value.empty() && value.front() == L'+')
			value.remove_prefix(1);
		if (value.empty())
			return false;

		std::size_t parsed{};
		for (const wchar_t character : value)
		{
			if (character < L'0' || character > L'9')
				return false;
			const std::size_t digit = static_cast<std::size_t>(character - L'0');
			if (parsed > ((std::numeric_limits<std::size_t>::max)() - digit) / 10)
				return false;
			parsed = parsed * 10 + digit;
		}
		outIndex = parsed;
		return true;
	}

	void ThrowIfOutOfMemory(const HRESULT result)
	{
		if (result == E_OUTOFMEMORY)
			throw std::bad_alloc{};
	}

	[[nodiscard]] HRESULT ReadPageAttributes(
		IXmlReader& reader, std::wstring& outType, std::wstring& outImage)
	{
		outType.clear();
		outImage.clear();
		HRESULT result = reader.MoveToFirstAttribute();
		if (result == S_FALSE)
			return S_OK;
		if (FAILED(result))
			return result;

		for (;;)
		{
			const wchar_t* localName{};
			UINT localNameLength{};
			result = reader.GetLocalName(&localName, &localNameLength);
			if (FAILED(result))
				return result;
			if (localName == nullptr)
				return E_UNEXPECTED;

			const wchar_t* value{};
			UINT valueLength{};
			result = reader.GetValue(&value, &valueLength);
			if (FAILED(result))
				return result;
			if (value == nullptr && valueLength != 0)
				return E_UNEXPECTED;
			const std::wstring_view name{ localName, localNameLength };
			if (EqualsAsciiCaseInsensitive(name, L"Type"))
			{
				if (valueLength == 0)
					outType.clear();
				else
					outType.assign(value, valueLength);
			}
			else if (EqualsAsciiCaseInsensitive(name, L"Image"))
			{
				if (valueLength == 0)
					outImage.clear();
				else
					outImage.assign(value, valueLength);
			}

			result = reader.MoveToNextAttribute();
			if (result == S_FALSE)
				break;
			if (FAILED(result))
				return result;
		}
		return reader.MoveToElement();
	}
}

namespace CbrCoverSelection
{
	bool NaturalLess(
		const std::wstring_view left, const std::wstring_view right) noexcept
	{
		return CompareNatural(left, right) < 0;
	}

	bool IsExplicitCoverName(const std::wstring_view fileName) noexcept
	{
		const std::size_t separator = fileName.find_last_of(L"/\\");
		const std::size_t nameBegin = separator == std::wstring_view::npos ? 0 : separator + 1;
		const std::size_t extension = fileName.find_last_of(L'.');
		const std::size_t nameEnd = extension == std::wstring_view::npos || extension < nameBegin
			? fileName.size()
			: extension;
		const std::wstring_view stem = fileName.substr(nameBegin, nameEnd - nameBegin);

		static constexpr std::array<std::wstring_view, 4> ExplicitNames
		{
			L"cover", L"front", L"frontcover", L"folder"
		};
		return std::ranges::any_of(ExplicitNames, [stem](const std::wstring_view name)
		{
			return EqualsNormalizedStem(stem, name);
		});
	}

	std::vector<std::size_t> FindFrontCoverImageIndices(
		const std::string_view comicInfoXml, const std::size_t imageCount)
	{
		std::vector<std::size_t> indices;
		if (comicInfoXml.empty() || imageCount == 0 ||
			imageCount > MaximumComicInfoPageTags)
			return indices;

		const Utility::DataIStream input{ comicInfoXml.data(), comicInfoXml.size() };
		if (FAILED(input.GetHResult()))
		{
			ThrowIfOutOfMemory(input.GetHResult());
			return indices;
		}

		ComPtr<IXmlReader> reader;
		HRESULT result = CreateXmlReader(
			__uuidof(IXmlReader), reinterpret_cast<void**>(reader.Put()), nullptr);
		if (FAILED(result))
		{
			ThrowIfOutOfMemory(result);
			return indices;
		}
		if (FAILED(result = reader->SetProperty(
			XmlReaderProperty_ConformanceLevel, XmlConformanceLevel_Document)) ||
			FAILED(result = reader->SetProperty(
				XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
			FAILED(result = reader->SetProperty(
				XmlReaderProperty_MaxElementDepth, MaximumXmlElementDepth)) ||
			FAILED(result = reader->SetProperty(
				XmlReaderProperty_MaxEntityExpansion, MaximumXmlEntityExpansion)) ||
			FAILED(result = reader->SetInput(input.GetIStream())))
		{
			ThrowIfOutOfMemory(result);
			return indices;
		}

		std::vector<unsigned char> seen(imageCount);
		bool foundRoot{};
		bool insidePages{};
		bool finishedPages{};
		UINT pagesDepth{};
		std::size_t pageCount{};
		for (;;)
		{
			XmlNodeType nodeType{ XmlNodeType_None };
			result = reader->Read(&nodeType);
			if (result == S_FALSE)
				return foundRoot ? indices : std::vector<std::size_t>{};
			if (FAILED(result))
			{
				ThrowIfOutOfMemory(result);
				return {};
			}

			if (nodeType != XmlNodeType_Element && nodeType != XmlNodeType_EndElement)
				continue;
			const wchar_t* localName{};
			UINT localNameLength{};
			UINT depth{};
			if (FAILED(result = reader->GetLocalName(&localName, &localNameLength)) ||
				localName == nullptr ||
				FAILED(result = reader->GetDepth(&depth)))
			{
				ThrowIfOutOfMemory(result);
				return {};
			}
			const std::wstring_view name{ localName, localNameLength };

			if (nodeType == XmlNodeType_EndElement)
			{
				if (insidePages && depth == pagesDepth &&
					EqualsAsciiCaseInsensitive(name, L"Pages"))
				{
					insidePages = false;
					finishedPages = true;
				}
				continue;
			}

			if (!foundRoot)
			{
				if (depth != 0 || !EqualsAsciiCaseInsensitive(name, L"ComicInfo"))
					return {};
				foundRoot = true;
				continue;
			}
			if (!insidePages && !finishedPages && depth == 1 &&
				EqualsAsciiCaseInsensitive(name, L"Pages"))
			{
				insidePages = reader->IsEmptyElement() == FALSE;
				finishedPages = !insidePages;
				pagesDepth = depth;
				continue;
			}
			if (!insidePages || depth != pagesDepth + 1 ||
				!EqualsAsciiCaseInsensitive(name, L"Page"))
				continue;

			if (++pageCount > MaximumComicInfoPageTags)
				return {};
			std::wstring type;
			std::wstring image;
			if (FAILED(result = ReadPageAttributes(*reader.Get(), type, image)))
			{
				ThrowIfOutOfMemory(result);
				return {};
			}
			if (!HasFrontCoverToken(type))
				continue;

			std::size_t imageIndex{};
			if (!TryParseComicInfoIndex(image, imageIndex) || imageIndex >= imageCount ||
				seen[imageIndex] != 0)
				continue;
			seen[imageIndex] = 1;
			indices.push_back(imageIndex);
		}
	}
}
