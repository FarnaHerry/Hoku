#pragma once

#include <huxerui/state.h>
#include <huxerui/view.h>

#include "browser_session.h"

namespace hoku {

[[huxerui::composable]]
huxerui::View BrowserTabStrip(huxerui::State<BrowserSession> session);

} // namespace hoku
