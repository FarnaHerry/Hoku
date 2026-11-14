#include "destination.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace hoku {
namespace {

constexpr std::string_view kSearchPrefix = "https://www.google.com/search?q=";

bool IsAsciiSpace(char value) {
  constexpr std::string_view kWhitespace = " \t\n\r\f\v";
  return kWhitespace.find(value) != std::string_view::npos;
}

std::string_view Trim(std::string_view value) {
  while (!value.empty() && IsAsciiSpace(value.front())) {
    value.remove_prefix(1);
  }
  while (!value.empty() && IsAsciiSpace(value.back())) {
    value.remove_suffix(1);
  }
  return value;
}

bool StartsWithInsensitive(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  return std::ranges::equal(value.substr(0, prefix.size()), prefix, [](char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
  });
}

bool HasNumericPort(std::string_view value, std::size_t colon) {
  if (colon == std::string_view::npos || colon + 1 == value.size()) {
    return false;
  }
  const std::string_view suffix = value.substr(colon + 1);
  const std::size_t port_end = suffix.find_first_of("/?#");
  const std::string_view port = suffix.substr(0, port_end);
  return !port.empty() &&
         std::ranges::all_of(port, [](char part) {
           return part >= '0' && part <= '9';
         });
}

bool HasUriScheme(std::string_view value) {
  const std::size_t colon = value.find(':');
  if (colon == std::string_view::npos || colon == 0 ||
      !std::isalpha(static_cast<unsigned char>(value.front()))) {
    return false;
  }
  const std::string_view scheme = value.substr(0, colon);
  if (!std::ranges::all_of(scheme, [](char part) {
        const unsigned char byte = static_cast<unsigned char>(part);
        return std::isalnum(byte) || part == '+' || part == '-' || part == '.';
      })) {
    return false;
  }

  // Address-bar users expect host:port, including intranet names, rather than
  // treating the host as an opaque URI scheme.
  return !HasNumericPort(value, colon);
}

bool IsLocalAddress(std::string_view value) {
  constexpr std::string_view kLocalhost = "localhost";
  if (StartsWithInsensitive(value, kLocalhost)) {
    const std::size_t boundary = kLocalhost.size();
    return value.size() == boundary ||
           std::string_view{":/?#"}.find(value[boundary]) != std::string_view::npos;
  }
  return value.starts_with("127.") || value.starts_with("[::1]") ||
         value.starts_with("0.0.0.0");
}

bool LooksLikeHost(std::string_view value) {
  if (std::ranges::any_of(value, IsAsciiSpace)) {
    return false;
  }
  const std::size_t colon = value.find(':');
  return value.find('.') != std::string_view::npos || HasNumericPort(value, colon) ||
         value.starts_with('[');
}

std::string EncodeSearchQuery(std::string_view value) {
  constexpr std::array<char, 16> kHex = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
  };
  std::string encoded;
  encoded.reserve(value.size());
  for (const unsigned char byte : value) {
    if (std::isalnum(byte) || byte == '-' || byte == '.' || byte == '_' || byte == '~') {
      encoded.push_back(static_cast<char>(byte));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex[byte >> 4U]);
      encoded.push_back(kHex[byte & 0x0FU]);
    }
  }
  return encoded;
}

} // namespace

std::string ResolveDestination(std::string_view input) {
  const std::string_view value = Trim(input);
  if (value.empty()) {
    return {};
  }
  if (value.starts_with("//")) {
    return "https:" + std::string(value);
  }
  if (IsLocalAddress(value)) {
    return "http://" + std::string(value);
  }
  if (HasUriScheme(value)) {
    return std::string(value);
  }
  if (LooksLikeHost(value)) {
    return "https://" + std::string(value);
  }
  return std::string(kSearchPrefix) + EncodeSearchQuery(value);
}

std::string HttpOrigin(std::string_view url) {
  const std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return {};
  }
  const std::string_view scheme = url.substr(0, scheme_end);
  if (!StartsWithInsensitive("https", scheme) && !StartsWithInsensitive("http", scheme)) {
    return {};
  }
  if (scheme.size() != 4 && scheme.size() != 5) {
    return {};
  }
  const std::size_t authority_start = scheme_end + 3;
  if (authority_start >= url.size()) {
    return {};
  }
  const std::size_t authority_end = url.find_first_of("/?#", authority_start);
  if (authority_end == authority_start) {
    return {};
  }
  return std::string(url.substr(0, authority_end));
}

} // namespace hoku
