#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>

#include <huxerui/state.h>
#include <huxerui/gesture.h>
#include <huxerui/task.h>
#include <huxerui/text_input.h>
#include <huxerui/theme.h>
#include <huxerui/view.h>
#include <huxerui/window.h>

#include "browser_session.h"
#include "browser_preferences.h"
#include "favicon.h"

namespace hoku {

struct TabEditor {
  huxerui::State<huxerui::TextEditingValue> value;
  huxerui::State<bool> dirty;
};

struct TabDragOrigin {
  TabInfo tab;
  std::size_t index;
  bool active;
};

struct TabSlide {
  TabId id;
  float left;
  std::optional<float> detached_width;
  bool detaching = false;
  bool operator==(const TabSlide&) const = default;
};

struct BrowserWindowState {
  explicit BrowserWindowState(BrowserSession initial = {}, huxerui::State<bool> keep_tab_close_position = huxerui::State(true));

  huxerui::State<BrowserSession> session;
  huxerui::State<FaviconMap> favicons;
  huxerui::State<bool> keep_tab_close_position;
  huxerui::State<std::shared_ptr<const TabDragOrigin>> drag_origin{std::shared_ptr<const TabDragOrigin>{}};
  huxerui::State<std::optional<TabSlide>> tab_slide{std::optional<TabSlide>{}};
  std::map<TabId, TabEditor> editors;
  huxerui::Rect tab_strip_bounds;
  float title_bar_width = 0.0F;
  float tab_row_left = 0.0F;
  std::map<TabId, huxerui::Rect> tab_bounds;
};

struct BrowserTabDrag {
  std::shared_ptr<BrowserWindowState> source;
  huxerui::WindowHandle window;
  TabId id;
};

struct TabDragHandlers {
  std::function<void(const huxerui::DragEvent&)> started;
  std::function<void(const huxerui::DragEvent&)> changed;
  std::function<void(const huxerui::DragEvent&)> ended;
  std::function<void()> canceled;
  std::function<void()> source_ready;
};

void CopyTabUiState(const BrowserTabDrag& tab, BrowserWindowState& destination);
void CloseBrowserTab(const BrowserTabDrag& tab);
void RunBrowserAction(BrowserAction action, std::shared_ptr<BrowserWindowState> model,
                      huxerui::WindowHandle window, huxerui::TaskScope tasks);
void DropBrowserTab(
    const BrowserTabDrag& tab,
    const std::shared_ptr<BrowserWindowState>& destination,
    huxerui::WindowHandle window,
    std::optional<TabId> anchor,
    bool after
);

[[huxerui::composable]]
huxerui::View BrowserWindow(
    std::shared_ptr<BrowserWindowState> model,
    huxerui::State<std::optional<huxerui::ThemeBrightness>> theme_override,
    huxerui::State<BrowserPreferences> preferences,
    std::function<void(TabId)> detach_tab,
    TabDragHandlers drag,
    bool fullscreen
);

} // namespace hoku
