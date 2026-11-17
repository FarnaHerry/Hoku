#include "browser_ui.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <app_resources.h>
#include <huxerui/huxerui.h>
#include <huxerui/webview.h>

#include "browser_session.h"
#include "browser_settings.h"
#include "browser_tabs.h"
#include "destination.h"

namespace hoku {
namespace {

using namespace huxerui;

constexpr float kLoadingIndicatorHeight = 2.0F;
constexpr float kMinimumWebViewHeight = 240.0F;
constexpr float kToolbarButtonSize = 40.0F;
constexpr float kAddressFieldHeight = 40.0F;
constexpr float kAddressFieldCornerRadius = 20.0F;
constexpr float kToolbarCornerRadius = 8.0F;
constexpr char kBlankPageUrl[] = "about:blank";

struct WebViewStartupBackground {
  class Extension;
  Color color;
  bool operator==(const WebViewStartupBackground&) const = default;
};

class WebViewStartupBackground::Extension final : public NodeExtension {
public:
  Extension(ViewNode&, const WebViewStartupBackground& spec) : color_(spec.color) {}

  void Update(ViewNode&, const WebViewStartupBackground& spec) {
    if (color_ != spec.color) {
      color_ = spec.color;
      InvalidatePaint(PaintInvalidation::Foreground);
    }
  }

  void PaintAboveContent(const ViewNode& node, PaintContext& paint) const override {
    paint.DrawRect(node.ContentBounds(), color_);
  }

private:
  Color color_;
};

const std::string& NewTabUrl(bool chinese) {
  const auto build = [](std::string_view language, std::string_view title, std::string_view instruction) {
    std::string html = R"html(
<!doctype html>
<html lang="{language}">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'">
<title>{title}</title>
<style>
  :root { color-scheme: light dark; font: 16px system-ui, sans-serif; }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: Canvas; color: CanvasText; }
  main { padding: 24px; text-align: center; }
  h1 { margin: 0 0 12px; font-size: 1.5rem; font-weight: 600; }
  p { margin: 0; }
</style>
<main><h1>Hoku</h1><p>{instruction}</p></main>
</html>
)html";
    html.replace(html.find("{language}"), sizeof("{language}") - 1, language);
    html.replace(html.find("{title}"), sizeof("{title}") - 1, title);
    html.replace(html.find("{instruction}"), sizeof("{instruction}") - 1, instruction);
    return "data:text/html;charset=utf-8," + EncodeUrlComponent(html);
  };
  static const auto english = build("en", "New tab", "Search or enter an address above.");
  static const auto simplified_chinese = build("zh-CN", "新标签页", "在上方搜索或输入网址。");
  return chinese ? simplified_chinese : english;
}

bool IsNewTabUrl(std::string_view url) {
  return url == NewTabUrl(false) || url == NewTabUrl(true);
}

template <class Events = WebViewEvents>
View WithWebViewFocus(View view, ViewHandle focus) {
  if constexpr (requires { typename Events::FocusEntered; }) {
    view = std::move(view).On<typename Events::FocusEntered>([focus] { focus.RequestFocus(); });
  }
  return std::move(view).With(focus);
}

std::string_view DisplayUrl(std::string_view url) {
  return IsNewTabUrl(url) ? std::string_view{} : url;
}

WebViewProperties MakeWebViewProperties(std::string url, const ThemeSpec& theme) {
  const Color background = IsNewTabUrl(url) ? theme.colors.background : Color::Rgb(255, 255, 255);
  return WebViewProperties{
      .url = std::move(url),
      .java_script_enabled = true,
      .theme_brightness = theme.brightness,
      .background_color = background,
  };
}

template <class Events = WebViewEvents>
View WithWebViewShortcuts(View view, WindowHandle window, TaskScope tasks, State<BrowserPreferences> preferences,
                          std::shared_ptr<BrowserWindowState> model) {
  if constexpr (requires { typename Events::ShortcutPressed; }) {
    return std::move(view).On<typename Events::ShortcutPressed>(
        [window, tasks, preferences, model](const KeyEvent& event) {
          const auto& shortcuts = preferences->shortcuts;
          const auto shortcut = std::ranges::find(shortcuts, BrowserShortcut{event.key, event.modifiers});
          if (shortcut == shortcuts.end()) return false;
          RunBrowserAction(static_cast<BrowserAction>(shortcut - shortcuts.begin()), model, window, tasks);
          return true;
        });
  }
  if constexpr (requires { typename Events::FullscreenShortcut; }) {
    return std::move(view).On<typename Events::FullscreenShortcut>([window, tasks, preferences, model] {
      const auto& shortcuts = preferences->shortcuts;
      const auto key = std::ranges::find(shortcuts, BrowserShortcut{Key::F11, {}});
      if (key != shortcuts.end()) {
        RunBrowserAction(static_cast<BrowserAction>(key - shortcuts.begin()), model, window, tasks);
      }
      return true;
    });
  }
  return view;
}

template <class Events = WebViewEvents>
View WithWebViewNewWindowRequest(View view, State<BrowserSession> session, TaskScope tasks) {
  if constexpr (requires { typename Events::NewWindowRequested; }) {
    return std::move(view).On<typename Events::NewWindowRequested>(
        [session, tasks](const auto& request) {
          if (request.user_initiated && !HttpOrigin(request.url).empty()) {
            // Mount the new WebView after the engine's synchronous callback returns.
            tasks.Post([session, url = request.url] {
              session.Update([&url](BrowserSession& current) { current.NewTab(url); });
            });
          }
          return true;
        });
  }
  return view;
}

[[huxerui::composable]]
View BrowserTab(
    TabId id,
    TabKind kind,
    std::string tab_url,
    TabEditor editor,
    std::shared_ptr<BrowserWindowState> model,
    State<std::optional<ThemeBrightness>> theme_override,
    State<BrowserPreferences> preferences,
    State<bool> keep_tab_close_position,
    State<FaviconMap> favicons,
    bool active,
    bool fullscreen
) {
  const auto session = model->session;
  const bool is_settings = kind == TabKind::Settings;
  const auto& new_tab_url = NewTabUrl(UseEnvironment<Locale>().LanguageTag().starts_with("zh"));
  auto requested_url = UseState(tab_url.empty() ? new_tab_url : tab_url);
  Lifecycle([requested_url, new_tab_url] {
    if (IsNewTabUrl(requested_url.Get()) && requested_url.Get() != new_tab_url) requested_url = new_tab_url;
  }, new_tab_url);
  auto show_startup_background = UseState(tab_url.empty());
  auto navigation = UseState(WebViewNavigationState{.url = is_settings ? kSettingsUrl : ""});
  auto address = editor.value;
  auto address_editing = editor.dirty;
  auto error = UseState(std::string{});
  auto stop_requested = UseState(false);
  auto favicon_origin = UseState(std::string{});
  WebViewController controller = UseWebViewController();
  Lifecycle([controller, active, is_settings] {
    if (!is_settings) {
      controller.SetMemoryUsageTarget(active ? WebViewMemoryUsageTarget::Normal : WebViewMemoryUsageTarget::Low);
    }
  }, active, is_settings);
  auto http = UseService<HttpClient>();
  auto tasks = UseTaskScope();
  auto browser_menu = UseMenu();
  auto menu_target = UseViewHandle();
  const auto web_view_focus = UseViewHandle();
  const ThemeSpec& theme = UseTheme();
  auto toolbar_button_style = detail::DefaultIconButtonStyle(theme);
  toolbar_button_style.minimum_interactive_size = kToolbarButtonSize;
  toolbar_button_style.state_layer_size = kToolbarButtonSize;
  toolbar_button_style.corner_radius = kToolbarButtonSize * 0.5F;
  auto address_style = UseEnvironment<TextFieldStyle>();
  address_style.show_label = false;
  address_style.text_style.foreground = theme.colors.on_surface;
  address_style.placeholder_style.foreground = theme.colors.on_surface_variant;
  address_style.disabled_text = theme.colors.on_surface;
  address_style.disabled_placeholder = theme.colors.on_surface_variant;
  address_style.leading_icon = theme.colors.on_surface_variant;
  address_style.focused_leading_icon = theme.colors.on_surface;
  address_style.disabled_leading_icon = theme.colors.on_surface_variant;
  address_style.outlined.background = theme.colors.surface_container;
  address_style.outlined.border = theme.colors.outline;
  address_style.outlined.border.alpha *= 0.55F;
  address_style.outlined.hovered_border = theme.colors.on_surface;
  address_style.outlined.focused_border = theme.colors.primary;
  address_style.outlined.corner_radii = CornerRadii{kAddressFieldCornerRadius};
  address_style.outlined.minimum_height = kAddressFieldHeight;
  address_style.padding = EdgeInsets::Symmetric(16.0F, 8.0F);
  auto navigate = [address, address_editing, requested_url, error, stop_requested,
                   controller, web_view_focus, tasks](std::string_view input) {
    std::string destination = ResolveDestination(input);
    if (destination.empty()) {
      return;
    }
    stop_requested = false;
    address_editing = false;
    address = TextEditingValue::FromText(destination);
    error = {};
    if (destination == requested_url.Get()) {
      controller.Reload();
    }
    requested_url = std::move(destination);
    tasks.Post([web_view_focus] { web_view_focus.RequestFocus(); });
  };
  auto submit = [navigate, address] {
    navigate(address->text);
  };

  const bool show_site_controls = !is_settings && !IsNewTabUrl(requested_url.Get());
  const bool is_loading_site = show_site_controls && navigation->is_loading;
  View loading_indicator = Divider().With(Frame{.height = kLoadingIndicatorHeight});
  if (is_loading_site) {
    const float progress = std::clamp(static_cast<float>(navigation->progress), 0.0F, 1.0F);
    loading_indicator = ProgressBar(progress).With(Frame{.height = kLoadingIndicatorHeight});
  }

  View error_banner = Row {};
  const std::string& error_text = error.Get();
  if (!error_text.empty()) {
    error_banner = Row {
      Text(error_text, TextRole::Body)
          .With(Grow(), Foreground(theme.colors.error)),
      Button(app::strings::retry).OnClick([controller, error] {
        if (controller.Reload()) {
          error = {};
        }
      }),
    }.With(
        Padding(EdgeInsets::Symmetric(12.0F, 6.0F)),
        Spacing(12.0F),
        CrossAlign(CrossAxisAlignment::Center),
        Background(theme.colors.surface_container)
    );
  }

  View page;
  if (is_settings) {
    page = SettingsContent(theme_override, keep_tab_close_position, preferences);
  } else {
    page = WebView(MakeWebViewProperties(requested_url, theme), controller)
        .On<WebViewEvents::NavigationChanged>(
            [id, session, address, address_editing, navigation, show_startup_background,
             requested_url, favicon_origin, favicons, http, tasks](const WebViewNavigationState& state) {
              if (session->FindTab(id) == nullptr) {
                return;
              }
              const bool new_tab = IsNewTabUrl(state.url);
              // Ignore the engine's initial empty document and late startup events while opening the first site.
              if (state.url.empty() ||
                  (state.url == kBlankPageUrl && navigation->url.empty() && requested_url.Get() != kBlankPageUrl) ||
                  (new_tab && DisplayUrl(navigation->url).empty() && requested_url.Get() != state.url)) {
                return;
              }
              if (!state.is_loading) show_startup_background = false;
              const bool metadata_changed = state.url != navigation->url ||
                                            state.title != navigation->title;
              const std::string_view display_url = DisplayUrl(state.url);
              if (!address_editing && display_url != address->text) {
                address = TextEditingValue::FromText(std::string(display_url));
              }
              navigation = state;
              requested_url = state.url;
              if (metadata_changed) {
                session.Update([id, &state, display_url, new_tab](BrowserSession& current) {
                  current.UpdateTab(id, std::string(display_url), new_tab ? "New tab" : state.title);
                });
              }
              RefreshFavicon(http, tasks, favicons, session, favicon_origin, id, state.url);
            }
        )
        .On<WebViewEvents::LoadStarted>([error](const WebViewNavigationState&) {
          error = {};
        })
        .On<WebViewEvents::LoadFinished>([stop_requested, show_startup_background](const WebViewNavigationState&) {
          stop_requested = false;
          show_startup_background = false;
        })
        .On<WebViewEvents::LoadFailed>(
            [error, stop_requested, navigation, fallback = UseString(app::strings::load_failed)](const WebViewLoadError& value) {
              const bool was_stopped = stop_requested;
              stop_requested = false;
              if (was_stopped ||
                  (value.url && !navigation->url.empty() && *value.url != navigation->url)) {
                return;
              }
              error = !value.message.empty() ? value.message
                                             : (!value.code.empty() ? value.code : fallback);
            }
        )
        .With(Grow(), Frame{.min_height = kMinimumWebViewHeight}, ClipChildren())
        .Key("tab-web-view");
    if (show_startup_background.Get()) {
      // Keep the startup paint on the native node so it never becomes a separate hit target.
      page = std::move(page).With(WebViewStartupBackground{theme.colors.background});
    }
    page = WithWebViewShortcuts(std::move(page), UseWindow(), UseApplicationTaskScope(), preferences, model);
    page = WithWebViewNewWindowRequest(std::move(page), session, tasks);
    page = WithWebViewFocus(std::move(page), web_view_focus);
  }

  View toolbar = Row {
    IconButton(app::images::arrow_back, app::strings::back)
        .OnClick([controller] {
          controller.GoBack();
        })
        .With(Enabled(navigation->can_go_back), Tooltip(app::strings::back)),
    IconButton(app::images::arrow_forward, app::strings::forward)
        .OnClick([controller] {
          controller.GoForward();
        })
        .With(Enabled(navigation->can_go_forward), Tooltip(app::strings::forward)),
    IconButton(
        is_loading_site ? app::images::close : app::images::refresh,
        is_loading_site ? app::strings::stop_loading : app::strings::reload
    )
        .OnClick([controller, navigation, stop_requested] {
          if (navigation->is_loading) {
            stop_requested = true;
            controller.StopLoading();
          } else {
            stop_requested = false;
            controller.Reload();
          }
        })
        .With(
            Enabled(show_site_controls), Tooltip(is_loading_site ? app::strings::stop_loading : app::strings::reload)
        ),
    Theme(
        ThemeDefinition{}.Set(std::move(address_style)),
        TextField(address)
            .Label(app::strings::address_label)
            .LeadingIcon(app::images::search)
            .Placeholder(app::strings::address_placeholder)
            .Variant(TextFieldVariant::Outlined)
            .InputConfiguration({
                .type = TextInputType::Url,
                .action = TextInputAction::Go,
                .read_only = is_settings,
            })
            .OnChanged([address, address_editing](const TextEditingValue& value) {
              address_editing = true;
              address = value;
            })
            .OnSubmitted(submit)
            .Key("address-field")
            .With(Grow())
    ),
    IconButton(app::images::more_vert, app::strings::browser_menu)
        .With(
            menu_target,
            Frame{.width = kToolbarButtonSize, .height = kToolbarButtonSize},
            Tooltip(app::strings::browser_menu)
        )
        .OnClick([browser_menu, menu_target, session] {
          browser_menu.Show(
              menu_target,
              {
                  MenuItem(app::images::settings, app::strings::settings, [session] {
                    session.Update([](BrowserSession& current) {
                      current.OpenSettings();
                    });
                  }),
              },
              MenuOptions{
                  .placement = {
                      .side = AnchorSide::Below,
                      .alignment = AnchorAlignment::End,
                  },
              }
          );
        })
        .Key("browser-menu"),
  }.With(
      Padding(EdgeInsets::Symmetric(8.0F, 6.0F)),
      Spacing(4.0F),
      CrossAlign(CrossAxisAlignment::Center),
      CornerRadius(CornerRadii::Top(kToolbarCornerRadius)),
      Background(theme.colors.surface_container_highest)
  ).Key(is_settings ? "settings-toolbar" : "browser-toolbar-" + std::to_string(id));

  return Column {
    fullscreen ? View{} : Theme(ThemeDefinition{}.Set(std::move(toolbar_button_style)), std::move(toolbar)),
    fullscreen ? View{} : std::move(loading_indicator),
    std::move(error_banner).Key("page-error"),
    is_settings ? std::move(page).Key("tab-content") : std::move(page),
  }.With(
      CrossAlign(CrossAxisAlignment::Stretch),
      Background(theme.colors.background)
  );
}

} // namespace

BrowserWindowState::BrowserWindowState(BrowserSession initial, State<bool> keep_tab_close_position)
    : session(std::move(initial)), favicons(FaviconMap{}), keep_tab_close_position(keep_tab_close_position) {}

void CopyTabUiState(const BrowserTabDrag& tab, BrowserWindowState& destination) {
  const auto editor = tab.source->editors.find(tab.id);
  if (editor != tab.source->editors.end()) {
    destination.editors.insert_or_assign(tab.id, editor->second);
  }
  const FaviconMap& existing_icons = tab.source->favicons.Get();
  const auto icon = existing_icons.find(tab.id);
  if (icon != existing_icons.end()) {
    destination.favicons.Update([&](FaviconMap& icons) { icons.insert_or_assign(tab.id, icon->second); });
  }
}

void CloseBrowserTab(const BrowserTabDrag& tab) {
  if (!tab.window.IsOpen() || !tab.source->session->FindTab(tab.id)) {
    return;
  }
  if (tab.source->session->Tabs().size() == 1) {
    tab.window.Close();
    return;
  }
  tab.source->session.Update([id = tab.id](BrowserSession& session) { session.CloseTab(id); });
  tab.source->editors.erase(tab.id);
  tab.source->tab_bounds.erase(tab.id);
  tab.source->favicons.Update([id = tab.id](FaviconMap& icons) { icons.erase(id); });
}

void RunBrowserAction(BrowserAction action, std::shared_ptr<BrowserWindowState> model,
                      WindowHandle window, TaskScope tasks) {
  tasks.Post([action, model = std::move(model), window] {
    if (!window.IsOpen()) return;
    switch (action) {
      case BrowserAction::NewTab:
        model->session.Update([](BrowserSession& current) { current.NewTab(); });
        break;
      case BrowserAction::CloseTab:
        CloseBrowserTab({model, window, model->session->ActiveTab()});
        break;
      case BrowserAction::Fullscreen:
        window.SetFullscreen(!window.IsFullscreen());
        break;
    }
  });
}

void DropBrowserTab(
    const BrowserTabDrag& tab,
    const std::shared_ptr<BrowserWindowState>& destination,
    WindowHandle window,
    std::optional<TabId> anchor,
    bool after
) {
  if (!tab.window.IsOpen() || !window.IsOpen()) {
    return;
  }
  const TabInfo* info = tab.source->session->FindTab(tab.id);
  if (info == nullptr) {
    return;
  }
  const auto& tabs = destination->session->Tabs();
  std::optional<TabId> before;
  if (anchor) {
    auto target = std::ranges::find(tabs, *anchor, &TabInfo::id);
    if (target == tabs.end()) {
      return;
    }
    if (after) {
      ++target;
    }
    if (target != tabs.end()) {
      before = target->id;
    }
  }
  BrowserSession next = destination->session.Get();
  if (tab.source == destination) {
    if (next.ReorderTab(tab.id, before)) {
      destination->session = std::move(next);
    }
    return;
  }
  if (next.InsertTab(*info, before)) {
    CopyTabUiState(tab, *destination);
    destination->session = std::move(next);
    CloseBrowserTab(tab);
    window.Activate();
  }
}

[[huxerui::composable]]
View BrowserWindow(
    std::shared_ptr<BrowserWindowState> model,
    State<std::optional<ThemeBrightness>> theme_override,
    State<BrowserPreferences> preferences,
    std::function<void(TabId)> detach_tab,
    TabDragHandlers drag,
    bool fullscreen
) {
  auto session = model->session;
  auto favicons = model->favicons;
  const ThemeSpec& theme = UseTheme();
  const BrowserSession& snapshot = session.Get();

  std::vector<View> pages;
  pages.reserve(snapshot.Tabs().size());
  for (const TabInfo& tab : snapshot.Tabs()) {
    auto [editor, inserted] = model->editors.try_emplace(tab.id);
    if (inserted) {
      editor->second = {State(TextEditingValue::FromText(tab.url)), State(false)};
    }
    pages.push_back(
        BrowserTab(tab.id, tab.kind, tab.url, editor->second, model, theme_override, preferences, model->keep_tab_close_position,
                   favicons, tab.id == snapshot.ActiveTab(), fullscreen)
            .Key(tab.id).With(Grow())
    );
  }

  View title_bar;
  if (!fullscreen) {
    title_bar = WithBrowserTabSeam(
        WindowTitleBar {
          BrowserTabSearchButton(session, favicons),
          BrowserTabStrip(model, std::move(detach_tab), std::move(drag)),
        }.With(
            Padding(EdgeInsets{.top = 3.0F, .right = 6.0F, .left = 6.0F}),
            Spacing(4.0F),
            CrossAlign(CrossAxisAlignment::Center),
            Background(theme.colors.surface_container)
        ),
        model,
        theme.colors.surface_container_highest
    );
  }

  return Column {
    std::move(title_bar),
    IndexedPages(std::move(pages), snapshot.ActiveIndex()).With(Grow()).Key("browser-pages"),
  }.With(
      CrossAlign(CrossAxisAlignment::Stretch),
      Background(theme.colors.background)
  );
}

} // namespace hoku
