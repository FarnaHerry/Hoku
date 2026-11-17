#pragma once

#include <functional>
#include <memory>

#include <huxerui/state.h>
#include <huxerui/view.h>

#include "browser_session.h"
#include "favicon.h"

namespace hoku {

struct BrowserWindowState;
struct TabDragHandlers;

void SlideBrowserTab(BrowserWindowState& model, TabId id, float left);
huxerui::View WithBrowserTabSeam(
    huxerui::View title_bar, std::shared_ptr<BrowserWindowState> model, huxerui::Color color
);

[[huxerui::composable]]
huxerui::View BrowserTabSearchButton(
    huxerui::State<BrowserSession> session,
    huxerui::State<FaviconMap> favicons
);

[[huxerui::composable]]
huxerui::View BrowserTabStrip(
    std::shared_ptr<BrowserWindowState> model,
    std::function<void(TabId)> detach_tab,
    TabDragHandlers drag
);

} // namespace hoku
