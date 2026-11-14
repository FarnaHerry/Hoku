#pragma once

#include <huxerui/state.h>
#include <huxerui/view.h>

#include "browser_session.h"
#include "favicon.h"

namespace hoku {

[[huxerui::composable]]
huxerui::View BrowserTabStrip(
    huxerui::State<BrowserSession> session,
    huxerui::State<FaviconMap> favicons
);

} // namespace hoku
