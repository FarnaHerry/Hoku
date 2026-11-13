#pragma once

#include <string>
#include <string_view>

namespace hoku {

/// Converts address-bar input into a navigable URL or a web-search URL.
/// An empty or whitespace-only input returns an empty string.
[[nodiscard]] std::string ResolveDestination(std::string_view input);

} // namespace hoku
