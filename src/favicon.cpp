#include "favicon.h"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "destination.h"

namespace hoku {
namespace {

using namespace huxerui;

constexpr std::array<std::string_view, 2> kFaviconPaths{
    "/apple-touch-icon.png",
    "/apple-touch-icon-precomposed.png",
};

bool HasTab(const BrowserSession& session, TabId id) {
  const auto tab = std::ranges::find_if(session.Tabs(), [id](const TabInfo& value) {
    return value.id == id;
  });
  return tab != session.Tabs().end();
}

Task<void> FetchFavicon(
    std::shared_ptr<HttpClient> http,
    State<FaviconMap> favicons,
    State<BrowserSession> session,
    State<std::string> loaded_origin,
    TabId id,
    std::string origin
) {
  for (const std::string_view path : kFaviconPaths) {
    HttpResult<HttpResponse> result = co_await http->SendAsync({
        .url = origin + std::string(path),
        .headers = {{"Accept", "image/png,image/jpeg;q=0.9"}},
    });
    if (!result.Succeeded()) {
      continue;
    }
    HttpResponse& response = result.Value();
    if (response.status_code < 200 || response.status_code >= 300 || response.body.empty()) {
      continue;
    }

    std::optional<ImageAsset> image;
    try {
      image = ImageAsset::FromEncoded(std::move(response.body));
    } catch (const std::invalid_argument&) {
      continue;
    }
    if (loaded_origin.Get() != origin || !HasTab(session.Get(), id)) {
      co_return;
    }
    favicons.Update([id, image = std::move(*image)](FaviconMap& values) mutable {
      values.insert_or_assign(id, std::move(image));
    });
    co_return;
  }
}

} // namespace

void RefreshFavicon(
    std::shared_ptr<HttpClient> http,
    TaskScope tasks,
    State<FaviconMap> favicons,
    State<BrowserSession> session,
    State<std::string> loaded_origin,
    TabId id,
    std::string url
) {
  std::string origin = HttpOrigin(url);
  if (origin.empty() || origin == loaded_origin.Get()) {
    return;
  }
  loaded_origin = origin;
  favicons.Update([id](FaviconMap& values) {
    values.erase(id);
  });
  tasks.Launch(FetchFavicon(
      std::move(http),
      favicons,
      session,
      loaded_origin,
      id,
      std::move(origin)
  ));
}

} // namespace hoku
