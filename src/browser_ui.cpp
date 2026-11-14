#include "browser_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
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
constexpr float kBrowserMenuButtonSize = 40.0F;
constexpr float kAddressFieldHeight = 40.0F;
constexpr float kAddressFieldCornerRadius = 20.0F;

std::string CssHex(Color color) {
  constexpr std::string_view kDigits = "0123456789ABCDEF";
  std::string value = "#";
  for (const float channel : {color.red, color.green, color.blue}) {
    const int byte = static_cast<int>(std::round(std::clamp(channel, 0.0F, 1.0F) * 255.0F));
    value.push_back(kDigits[byte >> 4]);
    value.push_back(kDigits[byte & 15]);
  }
  return value;
}

std::string WebThemeScript(const ThemeSpec& theme) {
  const bool dark = theme.brightness == ThemeBrightness::Dark;
  const std::string scheme = dark ? "dark" : "light";
  const std::string background = CssHex(theme.colors.background);
  const std::string foreground = CssHex(theme.colors.on_surface);
  return "(() => {"
         "const scheme = '" + scheme + "';"
         "const background = '" + background + "';"
         "const foreground = '" + foreground + "';"
         "const root = document.documentElement;"
         "root.style.colorScheme = scheme;"
         "root.style.setProperty('background', background, 'important');"
         "root.style.setProperty('color', foreground, 'important');"
         "if (document.body) {"
         "  document.body.style.setProperty('background', background, 'important');"
         "  document.body.style.setProperty('color', foreground, 'important');"
         "}"
         "let meta = document.querySelector('meta[name=\"color-scheme\"]');"
         "if (!meta) {"
         "  meta = document.createElement('meta');"
         "  meta.name = 'color-scheme';"
         "  (document.head || root).appendChild(meta);"
         "}"
         "meta.content = scheme === 'dark' ? 'dark light' : 'light dark';"
         "let style = document.getElementById('__hoku_theme__');"
         "if (!style) {"
         "  style = document.createElement('style');"
         "  style.id = '__hoku_theme__';"
         "  (document.head || root).appendChild(style);"
         "}"
         "style.textContent = ':root{color-scheme:' + scheme + '!important;' +"
         "'background:' + background + '!important;color:' + foreground + '!important}' +"
         "'body{background:' + background + '!important;color:' + foreground + '!important}';"
         "let themeColor = document.querySelector('meta[name=\"theme-color\"]');"
         "if (!themeColor) {"
         "  themeColor = document.createElement('meta');"
         "  themeColor.name = 'theme-color';"
         "  (document.head || root).appendChild(themeColor);"
         "}"
         "themeColor.content = background;"
         "})();";
}

void ApplyWebTheme(const WebViewController& controller, const std::string& script) {
  if (!controller.IsConnected()) {
    return;
  }
  controller.EvaluateJavaScript(script, [](PlatformResult<std::string>) {});
}

WebViewProperties MakeWebViewProperties(std::string url, ThemeBrightness brightness) {
  WebViewProperties properties{
      .url = std::move(url),
      .java_script_enabled = true,
  };
  properties.theme_brightness = brightness;
  return properties;
}

[[huxerui::composable]]
View NewTabContent() {
  const ThemeSpec& theme = UseTheme();
  return Column {
    Text("Hoku", TextRole::Title),
    Text("Search or enter an address above.", TextRole::Body)
        .With(Foreground(theme.colors.on_surface_variant)),
  }.With(
      Grow(),
      Padding(24.0F),
      Spacing(12.0F),
      MainAlign(MainAxisAlignment::Center),
      CrossAlign(CrossAxisAlignment::Center),
      Background(theme.colors.background)
  );
}

[[huxerui::composable]]
View BrowserTab(
    TabId id,
    TabKind kind,
    State<BrowserSession> session,
    State<std::optional<ThemeBrightness>> theme_override,
    State<FaviconMap> favicons
) {
  const bool is_settings = kind == TabKind::Settings;
  auto requested_url = UseState(std::string(is_settings ? kSettingsUrl : ""));
  auto navigation = UseState(WebViewNavigationState{.url = is_settings ? kSettingsUrl : ""});
  auto address = UseState(TextEditingValue::FromText(is_settings ? kSettingsUrl : ""));
  auto address_editing = UseState(false);
  auto error = UseState(std::string{});
  auto stop_requested = UseState(false);
  auto favicon_origin = UseState(std::string{});
  WebViewController controller = UseWebViewController();
  auto http = UseService<HttpClient>();
  auto tasks = UseTaskScope();
  auto browser_menu = UseMenu();
  auto menu_target = UseViewHandle();
  const ThemeSpec& theme = UseTheme();
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
  const std::string web_theme_script = WebThemeScript(theme);
  Lifecycle([controller, web_theme_script] {
    ApplyWebTheme(controller, web_theme_script);
  }, web_theme_script);

  auto navigate = [address, address_editing, requested_url, error, stop_requested,
                   controller](std::string_view input) {
    std::string destination = ResolveDestination(input);
    if (destination.empty()) {
      return;
    }
    stop_requested = false;
    address_editing = false;
    address = TextEditingValue::FromText(destination);
    error = {};
    requested_url = destination;
    if (controller.IsConnected()) {
      controller.LoadUrl(std::move(destination));
    }
  };
  auto submit = [navigate, address] {
    navigate(address->text);
  };

  View loading_indicator = Divider().With(Frame{.height = kLoadingIndicatorHeight});
  if (navigation->is_loading) {
    const float progress = std::clamp(static_cast<float>(navigation->progress), 0.0F, 1.0F);
    loading_indicator = ProgressBar(progress).With(Frame{.height = kLoadingIndicatorHeight});
  }

  View error_banner = Row {};
  const std::string& error_text = error.Get();
  if (!error_text.empty()) {
    error_banner = Row {
      Text(error_text, TextRole::Body)
          .With(Grow(), Foreground(theme.colors.error)),
      Button("Retry").OnClick([controller, error] {
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

  View page = is_settings ? SettingsContent(theme_override) : NewTabContent();
  if (!is_settings && !requested_url->empty()) {
    page = WebView(MakeWebViewProperties(requested_url, theme.brightness), controller)
        .On<WebViewEvents::NavigationChanged>(
            [id, session, address, address_editing, navigation,
             requested_url, favicon_origin, favicons, http, tasks](const WebViewNavigationState& state) {
              const bool metadata_changed = state.url != navigation->url ||
                                            state.title != navigation->title;
              const bool editing = address_editing;
              if (!editing && state.url != address->text) {
                address = TextEditingValue::FromText(state.url);
              }
              navigation = state;
              requested_url = state.url;
              if (metadata_changed) {
                session.Update([id, &state](BrowserSession& current) {
                  current.UpdateTab(id, state.url, state.title);
                });
              }
              RefreshFavicon(http, tasks, favicons, session, favicon_origin, id, state.url);
            }
        )
        .On<WebViewEvents::LoadStarted>([error](const WebViewNavigationState&) {
          error = {};
        })
        .On<WebViewEvents::LoadFinished>(
            [stop_requested, controller, web_theme_script](const WebViewNavigationState&) {
              stop_requested = false;
              ApplyWebTheme(controller, web_theme_script);
            }
        )
        .On<WebViewEvents::LoadFailed>(
            [error, stop_requested, navigation](const WebViewLoadError& value) {
              const bool was_stopped = stop_requested;
              stop_requested = false;
              if (was_stopped ||
                  (value.url && !navigation->url.empty() && *value.url != navigation->url)) {
                return;
              }
              error = !value.message.empty() ? value.message
                                             : (!value.code.empty() ? value.code
                                                                    : "Unable to load page.");
            }
        )
        .With(Grow(), Frame{.min_height = kMinimumWebViewHeight}, ClipChildren());
  }

  return Column {
    Row {
      IconButton(app::images::arrow_back, "Back")
          .OnClick([controller] {
            controller.GoBack();
          })
          .With(Enabled(navigation->can_go_back), Tooltip("Back")),
      IconButton(app::images::arrow_forward, "Forward")
          .OnClick([controller] {
            controller.GoForward();
          })
          .With(Enabled(navigation->can_go_forward), Tooltip("Forward")),
      IconButton(
          navigation->is_loading ? app::images::stop : app::images::refresh,
          navigation->is_loading ? "Stop loading" : "Reload"
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
              Enabled(!is_settings && (navigation->is_loading || !requested_url->empty())),
              Tooltip(navigation->is_loading ? "Stop loading" : "Reload")
          ),
      Theme(
          ThemeDefinition{}.Set(std::move(address_style)),
          TextField(address)
              .Label("Address and search")
              .LeadingIcon(app::images::search)
              .Placeholder("Search or enter address")
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
              .On<ViewEvents::FocusChanged>([address, address_editing, navigation](bool focused) {
                address_editing = false;
                if (!focused && address->text != navigation->url) {
                  address = TextEditingValue::FromText(navigation->url);
                }
              })
              .Key("address-field")
              .With(Grow())
      ),
      IconButton(app::images::more_vert, "Browser menu")
          .With(
              menu_target,
              Frame{.width = kBrowserMenuButtonSize, .height = kBrowserMenuButtonSize},
              Tooltip("Browser menu")
          )
          .OnClick([browser_menu, menu_target, session] {
            browser_menu.Show(
                menu_target,
                {
                    MenuItem(app::images::settings, "Settings", [session] {
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
        Background(theme.colors.surface_container_highest)
    ).Key(is_settings ? "settings-toolbar" : "browser-toolbar-" + std::to_string(id)),
    loading_indicator,
    error_banner,
    page,
  }.With(
      CrossAlign(CrossAxisAlignment::Stretch),
      Background(theme.colors.background)
  );
}

} // namespace

[[huxerui::composable]]
View BrowserWindow(State<std::optional<ThemeBrightness>> theme_override) {
  auto session = UseState(BrowserSession{});
  auto favicons = UseState(FaviconMap{});
  const ThemeSpec& theme = UseTheme();
  const BrowserSession& snapshot = session.Get();

  std::vector<View> pages;
  pages.reserve(snapshot.Tabs().size());
  for (const TabInfo& tab : snapshot.Tabs()) {
    pages.push_back(
        BrowserTab(tab.id, tab.kind, session, theme_override, favicons).Key(tab.id).With(Grow())
    );
  }

  return Column {
    WindowTitleBar {
      BrowserTabStrip(session, favicons),
    }.With(
        Padding(EdgeInsets{.top = 4.0F, .right = 6.0F, .left = 6.0F}),
        Spacing(4.0F),
        CrossAlign(CrossAxisAlignment::Center),
        Background(theme.colors.surface_container)
    ),
    IndexedPages(std::move(pages), snapshot.ActiveIndex()).With(Grow()),
  }.With(
      CrossAlign(CrossAxisAlignment::Stretch),
      Background(theme.colors.background)
  );
}

} // namespace hoku
