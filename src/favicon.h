#pragma once

#include <map>
#include <memory>
#include <string>

#include <huxerui/http.h>
#include <huxerui/resource.h>
#include <huxerui/state.h>
#include <huxerui/task.h>

#include "browser_session.h"

namespace hoku {

using FaviconMap = std::map<TabId, huxerui::ImageAsset>;

/// Starts at most one favicon request for the current tab origin.
void RefreshFavicon(
    std::shared_ptr<huxerui::HttpClient> http,
    huxerui::TaskScope tasks,
    huxerui::State<FaviconMap> favicons,
    huxerui::State<BrowserSession> session,
    huxerui::State<std::string> loaded_origin,
    TabId id,
    std::string url
);

} // namespace hoku
