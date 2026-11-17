#include <algorithm>
#include <chrono>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <app_resources.h>
#include <huxerui/huxerui.h>
#include <huxerui/webview.h>

#include "browser_tabs.h"
#include "browser_ui.h"

using namespace huxerui;

namespace {
constexpr Size kInitialWindowSize{1120.0F, 760.0F};
constexpr Size kMinimumWindowSize{640.0F, 480.0F};
constexpr float kTitleBarHeight = 42.0F;
const WindowOptions kBrowserWindowOptions{
    .title = "Hoku",
    .initial_size = kInitialWindowSize,
    .minimum_size = kMinimumWindowSize,
    .chrome_mode = WindowChromeMode::Custom,
    .title_bar_height = kTitleBarHeight,
};

using hoku::BrowserTabDrag;
using hoku::BrowserWindowState;
using ThemeOverride = State<std::optional<ThemeBrightness>>;

struct BrowserWindowEntry {
  std::weak_ptr<BrowserWindowState> model;
  WindowHandle window;
};

struct FloatingTabWindow {
  Point position;
  Size size;
};

struct PendingTabWindow {
  WindowOptions options;
  hoku::TabSlide detached;
  std::optional<hoku::TabSlide> previous_slide;
  Point grab;
  Point screen;
  bool released = false;
};

struct LiveTabDrag {
  BrowserTabDrag origin;
  BrowserTabDrag current;
  Point window_grab;
  Point tab_grab;
  bool moving_origin_window;
  std::optional<FloatingTabWindow> floating;
  std::shared_ptr<PendingTabWindow> pending;
};

struct BrowserWorkspace {
  std::vector<BrowserWindowEntry> windows;
  std::optional<LiveTabDrag> drag;
  State<hoku::BrowserPreferences> preferences{hoku::BrowserPreferences{}};
};

View BrowserApplicationWindow(
    std::shared_ptr<BrowserWorkspace> workspace,
    std::shared_ptr<BrowserWindowState> model,
    ThemeOverride theme_override
);

std::optional<BrowserTabDrag> OpenTabWindow(
    ApplicationHandle application,
    const std::shared_ptr<BrowserWorkspace>& workspace,
    const BrowserTabDrag& tab,
    ThemeOverride theme_override,
    ToastHandle toast,
    WindowOptions options,
    std::optional<hoku::TabSlide> slide = std::nullopt
) {
  if (!tab.window.IsOpen()) {
    return std::nullopt;
  }
  const auto* info = tab.source->session->FindTab(tab.id);
  if (info == nullptr) {
    return std::nullopt;
  }
  auto model = std::make_shared<BrowserWindowState>(
      tab.source->session->WindowWithTab(*info), tab.source->keep_tab_close_position);
  model->tab_slide = slide;
  hoku::CopyTabUiState(tab, *model);
  try {
    const auto window = application.OpenWindow(
        [workspace, model, theme_override] { return BrowserApplicationWindow(workspace, model, theme_override); },
        std::move(options));
    return BrowserTabDrag{std::move(model), window, tab.id};
  } catch (const std::runtime_error&) {
    toast.Show(app::strings::window_open_failed);
    return std::nullopt;
  }
}

void FinishLiveTab(BrowserWorkspace& workspace, const std::shared_ptr<BrowserWindowState>& origin, bool activate) {
  if (!workspace.drag || workspace.drag->origin.source != origin) {
    return;
  }
  if (activate && workspace.drag->pending) {
    workspace.drag->pending->released = true;
    return;
  }
  LiveTabDrag drag = std::move(*workspace.drag);
  workspace.drag.reset();
  origin->drag_origin = nullptr;
  drag.current.source->tab_slide = std::nullopt;
  if (drag.floating) {
    static_cast<void>(drag.current.window.SetTopmost(false));
    if (activate) {
      if (const auto area = drag.current.window.WorkArea(); area && !area->IsEmpty()) {
        const Point position = drag.current.window.Position().value_or(drag.floating->position);
        const Size size = drag.floating->size;
        const float min_x = area->x - size.width * 0.5F;
        const float min_y = area->y;
        const Point visible_position{
            std::clamp(position.x, min_x, min_x + area->width),
            std::clamp(position.y, min_y, min_y + std::max(0.0F, area->height - size.height * 0.5F)),
        };
        if (position != visible_position) {
          static_cast<void>(drag.current.window.SetPosition(visible_position));
        }
      }
    }
  }
  if (activate) {
    drag.current.window.Activate();
  }
}

void MoveLiveTab(BrowserWorkspace& workspace, const BrowserTabDrag& destination, std::optional<hoku::TabId> before) {
  auto& drag = *workspace.drag;
  auto next = destination.source->session.Get();
  const auto* info = drag.current.source->session->FindTab(drag.current.id);
  if (info == nullptr) {
    return;
  }
  if (next.InsertTab(*info, before)) {
    hoku::CopyTabUiState(drag.current, *destination.source);
    destination.source->session = std::move(next);
    const BrowserTabDrag source = drag.current;
    source.source->tab_slide = std::nullopt;
    drag.current = destination;
    drag.floating.reset();
    if (source.source == drag.origin.source && source.source->session->Tabs().size() == 1) {
      // Closing the window that owns the pointer capture ends this gesture.
      FinishLiveTab(workspace, source.source, true);
    }
    hoku::CloseBrowserTab(source);
  }
}

std::optional<hoku::TabId> TabBefore(const BrowserWindowState& model, Point point, hoku::TabId dragged) {
  for (const auto& tab : model.session->Tabs()) {
    const auto bounds = model.tab_bounds.find(tab.id);
    if (tab.id != dragged && bounds != model.tab_bounds.end() &&
        model.tab_strip_bounds.Intersects(bounds->second) &&
        point.x < bounds->second.x + bounds->second.width * 0.5F) {
      return tab.id;
    }
  }
  return std::nullopt;
}

bool DockLiveTab(BrowserWorkspace& workspace, LiveTabDrag& drag, Point screen) {
  for (auto entry = workspace.windows.rbegin(); entry != workspace.windows.rend(); ++entry) {
    auto model = entry->model.lock();
    const bool moving_window = model == drag.current.source && (drag.floating || drag.moving_origin_window);
    if (!model || moving_window || entry->window.IsFullscreen() ||
        entry->window.LifecycleState() == WindowLifecycleState::Background) {
      continue;
    }
    if (const auto origin = entry->window.Position()) {
      const Point local = screen - *origin;
      if (model->tab_strip_bounds.Contains(local)) {
        if (model == drag.current.source) {
          hoku::SlideBrowserTab(*model, drag.current.id, local.x - drag.tab_grab.x);
        } else {
          const auto before = TabBefore(*model, local, drag.current.id);
          MoveLiveTab(workspace, BrowserTabDrag{model, entry->window, drag.current.id}, before);
        }
        return true;
      }
    }
  }
  return false;
}

void OpenPendingTabWindow(
    ApplicationHandle application,
    const std::shared_ptr<BrowserWorkspace>& workspace,
    ThemeOverride theme_override,
    ToastHandle toast,
    const std::shared_ptr<PendingTabWindow>& pending
) {
  if (!workspace->drag || workspace->drag->pending != pending) {
    return;
  }
  auto& drag = *workspace->drag;
  const auto source = drag.current.source;
  const auto origin = drag.origin.source;
  drag.pending.reset();
  if (DockLiveTab(*workspace, drag, pending->screen)) {
    if (const auto slide = source->tab_slide.Get(); slide && slide->detaching) {
      source->tab_slide = pending->previous_slide;
    }
  } else {
    auto options = pending->options;
    options.initial_position = pending->screen - pending->grab;
    if (auto floating = OpenTabWindow(application, workspace, drag.current, theme_override, toast,
                                      options, pending->detached)) {
      source->tab_slide = std::nullopt;
      hoku::CloseBrowserTab(drag.current);
      drag.current = std::move(*floating);
      drag.window_grab = pending->grab;
      drag.floating = FloatingTabWindow{*options.initial_position, options.initial_size};
    } else {
      source->tab_slide = pending->previous_slide;
    }
  }
  if (pending->released) {
    FinishLiveTab(*workspace, origin, true);
  }
}

void UpdateLiveTab(
    BrowserWorkspace& workspace,
    const std::shared_ptr<BrowserWindowState>& origin_model,
    Point screen
) {
  if (!workspace.drag || workspace.drag->origin.source != origin_model) {
    return;
  }
  auto& drag = *workspace.drag;
  if (!drag.current.window.IsOpen()) {
    return;
  }
  if (drag.pending) {
    drag.pending->screen = screen;
    return;
  }
  if (DockLiveTab(workspace, drag, screen)) {
    return;
  }
  const Point position = screen - drag.window_grab;
  if (drag.moving_origin_window) {
    if (drag.origin.window.Position() != position) {
      static_cast<void>(drag.origin.window.SetPosition(position));
    }
    return;
  }
  if (drag.floating) {
    if (position != drag.floating->position &&
        drag.current.window.SetPosition(position) == WindowRequestResult::Submitted) {
      drag.floating->position = position;
    }
    return;
  }
  if (const auto docked = drag.current.window.Position()) {
    const auto strip = drag.current.source->tab_strip_bounds;
    const Rect margin{strip.x - 12.0F, strip.y - 12.0F, strip.width + 24.0F, strip.height + 24.0F};
    if (margin.Contains(screen - *docked)) {
      return;
    }
  }
  if (drag.current.source->session->Tabs().size() == 1) {
    return;
  }
  const auto& source = *drag.current.source;
  const auto it = source.tab_bounds.find(drag.current.id);
  if (it == source.tab_bounds.end() || it->second.IsEmpty()) {
    return;
  }
  const Rect tab = it->second;
  const Rect strip = source.tab_strip_bounds;
  const float left = std::max(0.0F, tab.x - strip.x);
  const Point grab{strip.x + left + std::min(drag.tab_grab.x, tab.width), tab.y + drag.tab_grab.y};
  auto options = kBrowserWindowOptions;
  // Match the presented tab position and leave room for the native caption buttons.
  const float right_inset = std::max(0.0F, source.title_bar_width - (strip.x + strip.width));
  options.initial_size.width = std::max(options.initial_size.width, strip.x + left + tab.width + right_inset);
  options.topmost = true;
  drag.pending = std::make_shared<PendingTabWindow>(PendingTabWindow{
      options, hoku::TabSlide{drag.current.id, left, tab.width}, source.tab_slide.Get(), grab, screen,
  });
  source.tab_slide = hoku::TabSlide{drag.current.id, left, std::nullopt, true};
}

void StartLiveTab(
    BrowserWorkspace& workspace,
    const std::shared_ptr<BrowserWindowState>& model,
    WindowHandle window,
    const DragEvent& event
) {
  if (workspace.drag || event.device_kind != PointerDeviceKind::Mouse || !window.IsOpen()) {
    return;
  }
  const Point down = event.window_position - (event.position - event.origin);
  const auto& tabs = model->session->Tabs();
  const auto tab = std::ranges::find_if(tabs, [&](const hoku::TabInfo& info) {
    const auto bounds = model->tab_bounds.find(info.id);
    return bounds != model->tab_bounds.end() && bounds->second.Contains(down);
  });
  if (tab != tabs.end()) {
    const bool single_tab = tabs.size() == 1;
    const auto bounds = model->tab_bounds.at(tab->id);
    const BrowserTabDrag source{model, window, tab->id};
    model->drag_origin = std::make_shared<hoku::TabDragOrigin>(hoku::TabDragOrigin{
        *tab, static_cast<std::size_t>(std::distance(tabs.begin(), tab)), tab->id == model->session->ActiveTab(),
    });
    workspace.drag = LiveTabDrag{
        .origin = source,
        .current = source,
        .window_grab = down,
        .tab_grab = down - Point{bounds.x, bounds.y},
        .moving_origin_window = single_tab,
    };
  }
}

[[huxerui::composable]]
View BrowserApplicationWindow(
    std::shared_ptr<BrowserWorkspace> workspace,
    std::shared_ptr<BrowserWindowState> model,
    ThemeOverride theme_override
) {
  const auto window = UseWindow();
  Lifecycle([workspace, model, window] {
    workspace->windows.push_back({model, window});
    return [workspace, model] {
      std::erase_if(workspace->windows, [&](const BrowserWindowEntry& entry) { return entry.model.lock() == model; });
    };
  }, model);
  const std::optional<ThemeBrightness> selected = theme_override;
  const ThemeBrightness brightness = selected ? *selected : UseSystemThemeBrightness();
  ThemeSpec theme = brightness == ThemeBrightness::Dark
                        ? FlatDarkThemeSpec()
                        : FlatLightThemeSpec();
  const auto application = UseApplication();
  const auto tasks = UseApplicationTaskScope();
  const auto toast = UseToast();
  const auto preferences = workspace->preferences;
  const auto shortcuts = preferences->shortcuts;
  for (std::size_t index = 0; index < shortcuts.size(); ++index) {
    const auto& shortcut = shortcuts[index];
    window.OnShortcut(shortcut.key, shortcut.modifiers, [model, window, tasks, index] {
      hoku::RunBrowserAction(static_cast<hoku::BrowserAction>(index), model, window, tasks);
      return true;
    }, index, shortcut);
  }
  if (std::ranges::find(shortcuts, hoku::BrowserShortcut{Key::F11, {}}) == shortcuts.end()) {
    window.OnShortcut(Key::F11, {}, [] { return true; });
  }
  const auto motion = UseState(std::make_shared<std::optional<Point>>()).Get();
  std::function<void(hoku::TabId)> detach_tab;
  if (application.SupportsMultipleWindows()) {
    detach_tab = [application, tasks, workspace, model, window, theme_override, toast](hoku::TabId id) {
      tasks.Post([application, workspace, tab = BrowserTabDrag{model, window, id}, theme_override, toast] {
        auto options = kBrowserWindowOptions;
        if (const auto origin = tab.window.Position()) {
          options.initial_position = *origin + Point{40.0F, 46.0F};
        }
        if (OpenTabWindow(application, workspace, tab, theme_override, toast, options)) {
          hoku::CloseBrowserTab(tab);
        }
      });
    };
  }
  hoku::TabDragHandlers drag;
  const auto capabilities = window.Capabilities();
  if (detach_tab && capabilities.geometry && capabilities.topmost) {
    const auto finish = [workspace, model](bool activate) {
      FinishLiveTab(*workspace, model, activate);
    };
    const auto update = [workspace, model](Point screen) {
      UpdateLiveTab(*workspace, model, screen);
    };
    drag.source_ready = [tasks, application, workspace, model, theme_override, toast] {
      if (workspace->drag && workspace->drag->current.source == model && workspace->drag->pending) {
        tasks.Launch([application, workspace, theme_override, toast,
                      pending = workspace->drag->pending]() -> Task<void> {
          // Allow the committed source strip to be presented before the destination is shown.
          co_await Delay(std::chrono::milliseconds(32));
          OpenPendingTabWindow(application, workspace, theme_override, toast, pending);
        });
      }
    };
    drag.started = [tasks, workspace, model, window](const DragEvent& event) {
      tasks.Post([workspace, model, window, event] { StartLiveTab(*workspace, model, window, event); });
    };
    drag.changed = [tasks, update, motion, window](const DragEvent& event) {
      const auto origin = window.Position();
      if (!origin) {
        return;
      }
      const bool queued = motion->has_value();
      // Capture screen coordinates before a queued update can move the source window.
      *motion = *origin + event.window_position;
      if (!queued) {
        tasks.Post([update, motion] {
          if (const auto point = std::exchange(*motion, std::nullopt)) {
            update(*point);
          }
        });
      }
    };
    drag.ended = [tasks, update, finish, window](const DragEvent& event) {
      const auto origin = window.Position();
      tasks.Post([update, finish, origin, client = event.window_position] {
        if (origin) {
          update(*origin + client);
        }
        finish(true);
      });
    };
    drag.canceled = [tasks, finish] { tasks.Post([finish] { finish(false); }); };
    window.OnCloseRequest([workspace, model, tasks] {
      if (workspace->drag && (workspace->drag->origin.source == model || workspace->drag->current.source == model)) {
        const auto origin = workspace->drag->origin.source;
        tasks.Post([workspace, origin] { FinishLiveTab(*workspace, origin, false); });
      }
      return false;
    }, model);
  }
  const auto language = preferences->language;
  View browser = Scope([model, theme_override, preferences, detach_tab = std::move(detach_tab),
                        drag = std::move(drag), fullscreen = window.IsFullscreen()] {
    return hoku::BrowserWindow(model, theme_override, preferences, detach_tab, drag, fullscreen);
  });
  const Locale locale = language == 0 ? UseEnvironment<Locale>()
                                      : Locale::FromLanguageTag(language == 2 ? "zh-CN" : "en");
  return FlatTheme(theme, ProvideEnvironment(locale, std::move(browser)));
}

} // namespace

View App() {
  auto workspace = UseState(std::make_shared<BrowserWorkspace>());
  auto model = UseState(std::make_shared<hoku::BrowserWindowState>());
  auto theme_override = UseState(std::optional<ThemeBrightness>{});
  return BrowserApplicationWindow(workspace.Get(), model.Get(), theme_override);
}

const Application application{
    App,
    {
        .window = kBrowserWindowOptions,
        .show_debug_overlay = false,
        .application_hooks = {
            huxerui::InstallWebView,
        },
    },
};
