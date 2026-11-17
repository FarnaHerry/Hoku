#include "favicon.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include "destination.h"

namespace hoku {
namespace {

using namespace huxerui;

bool HasTab(const BrowserSession& session, TabId id) {
  const auto tab = std::ranges::find_if(session.Tabs(), [id](const TabInfo& value) {
    return value.id == id;
  });
  return tab != session.Tabs().end();
}

void PublishFavicon(
    State<FaviconMap> favicons,
    State<BrowserSession> session,
    State<std::string> loaded_origin,
    TabId id,
    const std::string& origin,
    HttpResponse& response
) {
  if (response.status_code < 200 || response.status_code >= 300 || response.body.empty()) {
    return;
  }

  std::optional<ImageAsset> image;
  try {
    image = ImageAsset::FromEncoded(std::move(response.body));
  } catch (const std::invalid_argument&) {
    return;
  }
  if (loaded_origin.Get() != origin || !HasTab(session.Get(), id)) {
    return;
  }
  favicons.Update([id, image = std::move(*image)](FaviconMap& values) mutable {
    values.insert_or_assign(id, std::move(image));
  });
}

Task<void> FetchFavicon(
    std::shared_ptr<HttpClient> http,
    State<FaviconMap> favicons,
    State<BrowserSession> session,
    State<std::string> loaded_origin,
    TabId id,
    std::string origin
) {
  HttpRequest request{
      .url = origin + "/apple-touch-icon.png",
      .headers = {{"Accept", "image/png,image/jpeg;q=0.9"}},
  };
  HttpResult<HttpResponse> result = co_await http->SendAsync(request);
  if (result.Succeeded()) {
    PublishFavicon(favicons, session, loaded_origin, id, origin, result.Value());
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
  if (origin == loaded_origin.Get()) {
    return;
  }
  loaded_origin = origin;
  favicons.Update([id](FaviconMap& values) {
    values.erase(id);
  });
  if (origin.empty()) {
    return;
  }
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
