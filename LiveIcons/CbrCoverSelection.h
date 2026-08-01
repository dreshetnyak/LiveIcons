#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace CbrCoverSelection
{
	[[nodiscard]] bool NaturalLess(
		std::wstring_view left, std::wstring_view right) noexcept;

	[[nodiscard]] bool IsExplicitCoverName(std::wstring_view fileName) noexcept;

	[[nodiscard]] std::vector<std::size_t> FindFrontCoverImageIndices(
		std::string_view comicInfoXml, std::size_t imageCount);
}
