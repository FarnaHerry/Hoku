#pragma once

#include <string>
#include <string_view>

namespace hoku {

/// Converts address-bar input into a navigable URL or a web-search URL.
/// An empty or whitespace-only input returns an empty string.
[[nodiscard]] std::string ResolveDestination(std::string_view input);

/// Returns the HTTP(S) scheme and authority without path, query, or fragment.
/// Returns an empty string for non-HTTP absolute URLs.
[[nodiscard]] std::string HttpOrigin(std::string_view url);

} // namespace hoku
