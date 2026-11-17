#include <huxerui/huxerui.h>
#include <huxerui/testing/ui_test.h>
#include <huxerui/webview.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "browser_session.h"
#include "browser_tabs.h"
#include "browser_ui.h"

extern huxerui::View App();

namespace {

using namespace huxerui;
using namespace huxerui::testing;

class PackageResources final : public PlatformResources {
public:
  explicit PackageResources(std::string root) : root_(std::move(root)) {}

  ResourceConfiguration Configuration() const override {
    return {};
  }

  std::optional<InputStream> OpenRead(std::string_view path) override {
    auto result = File(root_ + "/" + std::string(path)).OpenRead();
    if (!result.Succeeded()) {
      return std::nullopt;
    }
    return std::move(result).Value();
  }

private:
  std::string root_;
};

void Expect(bool condition, std::string_view name) {
  if (!condition) {
    throw std::runtime_error(std::string(name) + ": condition failed");
  }
}

void ExpectSelected(UiTest& ui, std::string label) {
  Expect(ui.FindSemantics(UiSemanticSelector::AllOf(
             UiSemanticSelector::Label(std::move(label)),
             UiSemanticSelector::Selected(true)
         )).Exists(), "theme selection");
}

struct DragProbe {
  int moves = 0;
  bool ended = false;
  bool canceled = false;
  std::optional<float> tab_grab_x;
  std::optional<hoku::TabId> first_after_slide;
  int close_requests = 0;
};

DragProbe drag_probe;

[[huxerui::composable]]
View DragTestStrip(int tab_count, bool compact_test) {
  const auto source = UseState([tab_count, compact_test] {
    hoku::BrowserSession session;
    const auto first = session.ActiveTab();
    for (int index = 1; index < tab_count; ++index) {
      session.NewTab();
    }
    if (!compact_test) {
      session.SelectTab(first);
    }
    return std::make_shared<hoku::BrowserWindowState>(std::move(session));
  }()).Get();
  const auto window = UseWindow();
  if (compact_test) {
    window.OnShortcut(Key::F6, {}, [source] {
      const auto tab = source->session->Tabs().front();
      source->session = source->session->WindowWithTab(tab);
      source->tab_slide = hoku::TabSlide{tab.id, 230.0F, 96.0F};
      return true;
    }, source);
    window.OnShortcut(Key::F7, {}, [source] {
      source->tab_slide = std::nullopt;
      return true;
    }, source);
  }
  window.OnCloseRequest([] {
    ++drag_probe.close_requests;
    return true;
  }, 0);
  auto strip = hoku::BrowserTabStrip(source, {}, {
      .started = [source, window](const DragEvent& event) {
        const hoku::TabInfo tab = source->session->Tabs().front();
        if (event.translation.x != 0.0F) {
          drag_probe.tab_grab_x = event.window_position.x - event.translation.x - source->tab_bounds.at(tab.id).x;
          return;
        }
        source->drag_origin = std::make_shared<hoku::TabDragOrigin>(hoku::TabDragOrigin{tab, 0, true});
        hoku::CloseBrowserTab({source, window, tab.id});
      },
      .changed = [source](const DragEvent& event) {
        if (drag_probe.tab_grab_x) {
          hoku::SlideBrowserTab(*source, source->session->ActiveTab(), event.window_position.x - *drag_probe.tab_grab_x);
          drag_probe.first_after_slide = source->session->Tabs().front().id;
        }
        ++drag_probe.moves;
      },
      .ended = [source](const DragEvent&) {
        source->tab_slide = std::nullopt;
        source->drag_origin = nullptr;
        drag_probe.ended = true;
      },
      .canceled = [] { drag_probe.canceled = true; },
  });
  return hoku::WithBrowserTabSeam(
      WindowTitleBar{std::move(strip)}.With(Frame{.height = 42.0F}),
      source,
      UseTheme().colors.surface_container_highest
  );
}

std::string CaptureTabSeamOutline(UiTest& ui) {
  const auto snapshot = ui.CaptureSnapshot().ToString();
  const auto start = snapshot.find("fill-path");
  Expect(start != std::string::npos, "active tab paints a curved seam in the title bar");
  const auto end = snapshot.find("brush", start);
  Expect(end != std::string::npos, "tab seam has a filled outline");
  return snapshot.substr(start, end - start);
}

std::string CapturePaint(UiTest& ui) {
  const auto snapshot = ui.CaptureSnapshot().ToString();
  return snapshot.substr(snapshot.find("\n  render {\n"));
}

void ExpectDragSurvivesTabTransfer() {
  const Application application{[] { return DragTestStrip(2, false); }, {.show_debug_overlay = false}};
  UiTest ui(application, {
      .viewport = {800.0F, 400.0F},
      .resource_provider = std::make_shared<PackageResources>(HOKU_UI_TEST_PACKAGE),
  });
  ui.Pump(std::chrono::seconds(1));
  ui.Pump(std::chrono::seconds(1));
  const auto initial_seam = CaptureTabSeamOutline(ui);
  hoku::BrowserSession ids;
  const auto tab = ui.Find(UiSelector::Key(ids.ActiveTab())).Find(UiSelector::Key("tab-chip"));
  const auto neighbor_id = ids.NewTab();
  const auto neighbor = ui.Find(UiSelector::Key(neighbor_id)).Find(UiSelector::Key("tab-chip"));
  const Rect bounds = tab.One().bounds;
  const Point start{bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F};
  const auto titles = ui.Find(UiSelector::Text("New tab"));
  const auto title_count = titles.Count();
  const auto hover_at = [&](Point position) {
    ui.SendPointer({PointerEventType::Move, 1, position});
    ui.Pump();
    ui.Pump(std::chrono::seconds(1));
    ui.Pump();
  };
  const auto before_active_hover = CapturePaint(ui);
  hover_at(start);
  Expect(titles.Count() == title_count, "the selected tab does not display a hover tooltip");
  Expect(CapturePaint(ui) == before_active_hover, "hover leaves the selected tab paint unchanged");
  const Rect neighbor_bounds = neighbor.One().bounds;
  const Point neighbor_center{neighbor_bounds.x + neighbor_bounds.width * 0.5F,
                              neighbor_bounds.y + neighbor_bounds.height * 0.5F};
  hover_at(neighbor_center);
  Expect(titles.Count() == title_count + 1, "inactive tabs retain their hover information");
  hover_at(start);
  const PointerEvent down{
      .type = PointerEventType::Down,
      .pointer_id = 1,
      .position = start,
      .changed_button = PointerButton::Primary,
      .pressed_buttons = PointerButton::Primary,
  };
  ui.SendPointer(down);
  ui.Pump();
  ui.Pump(std::chrono::milliseconds(160));
  Expect(CapturePaint(ui) == before_active_hover, "holding the mouse down leaves the selected tab paint unchanged");
  PointerEvent pointer = down;
  pointer.type = PointerEventType::Move;
  pointer.changed_button = PointerButton::None;
  pointer.position.x += bounds.width * 0.2F;
  ui.SendPointer(pointer);
  ui.Pump();
  Expect(tab.One().bounds.x > bounds.x + 10.0F, "dragged tab follows the pointer before reordering");
  Expect(CaptureTabSeamOutline(ui) != initial_seam, "tab seam follows the dragged tab before reordering");
  Expect(neighbor.One().bounds.x > tab.One().bounds.x, "neighbor waits for the reorder threshold");
  pointer.position.x += bounds.width * 0.55F;
  ui.SendPointer(pointer);
  ui.Pump();
  Expect(!drag_probe.canceled, "sliding preserves capture between moves");
  Expect(drag_probe.first_after_slide == neighbor_id, "sliding right updates the session order before release");
  Expect(neighbor.One().bounds.x == neighbor_bounds.x, "neighbor starts from its previous visual position");
  ui.Pump(std::chrono::milliseconds(80));
  Expect(neighbor.One().bounds.x < neighbor_bounds.x && neighbor.One().bounds.x > bounds.x,
         "neighbor animates between its old and new slots");
  ui.Pump(std::chrono::milliseconds(160));
  Expect(neighbor.One().bounds.x < tab.One().bounds.x, "neighbor settles on the left before release");
  pointer.position = start;
  ui.SendPointer(pointer);
  ui.Pump();
  ui.Pump(std::chrono::milliseconds(160));
  Expect(tab.One().bounds.x < neighbor.One().bounds.x, "dragging left restores the original order");
  pointer.type = PointerEventType::Up;
  pointer.changed_button = PointerButton::Primary;
  pointer.pressed_buttons = PointerButton::None;
  ui.SendPointer(pointer);
  ui.Pump();
  Expect(drag_probe.ended && !drag_probe.canceled, "reordering preserves the active pointer route");
  Expect(CaptureTabSeamOutline(ui) == initial_seam, "tab seam settles back into the original slot");

  drag_probe = {};
  ui.SendPointer(down);
  pointer = down;
  pointer.type = PointerEventType::Move;
  pointer.changed_button = PointerButton::None;
  pointer.position.y += 60.0F;
  ui.SendPointer(pointer);
  ui.Pump();
  Expect(!tab.One().in_viewport && neighbor.One().bounds.x == bounds.x,
         "transferred tab disappears and its neighbor fills the slot on the first frame");
  ui.Pump(std::chrono::seconds(5));
  Expect(tab.One().bounds.width < bounds.width, "transferred source tab collapses");
  Expect(!drag_probe.canceled, "drag remains captured after source tab transfer and a pause");
  const int before_move = drag_probe.moves;
  pointer.position.y += 20.0F;
  ui.SendPointer(pointer);
  Expect(drag_probe.moves > before_move, "held drag can resume after the pause");
  pointer.type = PointerEventType::Up;
  pointer.changed_button = PointerButton::Primary;
  pointer.pressed_buttons = PointerButton::None;
  ui.SendPointer(pointer);
  Expect(drag_probe.ended && !drag_probe.canceled, "held drag ends on pointer release");
  ui.Pump();
  Expect(drag_probe.close_requests == 0 && neighbor.Exists(), "closing a tab keeps a window with other tabs open");
  ui.FindSemantics(UiSemanticSelector::Label("Close tab")).PerformSemanticAction({SemanticActionKind::Activate, {}});
  ui.Pump();
  Expect(drag_probe.close_requests == 1 && neighbor.Exists(), "closing the last tab requests window closure");

  const auto add_tab = ui.Find(UiSelector::Key("new-tab"));
  for (int index = 0; index < 3; ++index) {
    add_tab.Tap();
    ui.Pump();
  }
  ui.Pump(std::chrono::seconds(1));
  const auto separator_count = [&] {
    const auto paint = CapturePaint(ui);
    constexpr std::string_view line = "line {\n";
    std::size_t count = 0;
    for (auto at = paint.find(line); at != std::string::npos; at = paint.find(line, at + line.size())) {
      ++count;
    }
    return count;
  };
  Expect(separator_count() == 2, "separators appear only between adjacent inactive tabs");
  const auto center = [](Rect bounds) { return Point{bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F}; };
  const auto second = ui.Find(UiSelector::Key(neighbor_id + 1)).Find(UiSelector::Key("tab-chip"));
  const auto close_second = second.Find(UiSelector::Key("close-tab"));
  hover_at(center(second.One().bounds));
  Expect(separator_count() == 0, "hover hides both separators around a tab");
  hover_at(center(close_second.One().bounds));
  Expect(separator_count() == 0, "moving onto the close button keeps both separators hidden");
  hover_at({center(second.One().bounds).x, 100.0F});
  Expect(separator_count() == 2, "both separators return after leaving the tab from the close button");
  second.Tap();
  ui.Pump();
  Expect(separator_count() == 1, "moving the selection preserves only the separator away from it");

  add_tab.Tap();
  add_tab.Tap();
  ui.Pump(std::chrono::seconds(1));
  const auto third = ui.Find(UiSelector::Key(neighbor_id + 2)).Find(UiSelector::Key("tab-chip"));
  const auto fourth = ui.Find(UiSelector::Key(neighbor_id + 3)).Find(UiSelector::Key("tab-chip"));
  const auto mouse_click = [&](Point position) {
    ui.SendPointer({PointerEventType::Down, 1, position, PointerDeviceKind::Mouse,
                    PointerButton::Primary, PointerButton::Primary});
    ui.SendPointer({PointerEventType::Up, 1, position, PointerDeviceKind::Mouse, PointerButton::Primary});
    ui.Pump();
    ui.Pump();
  };
  const Point close_position = center(close_second.One().bounds);
  hover_at(close_position);
  const float original_width = second.One().bounds.width;
  const auto has_original_width = [original_width](const UiNodeQuery& tab) {
    return std::abs(tab.One().bounds.width - original_width) < 0.01F;
  };
  mouse_click(close_position);
  Expect(!ui.Find(UiSelector::Key(neighbor_id + 1)).Exists() && third.Exists(), "mouse closes the middle tab");
  Expect(has_original_width(third), "middle-tab close holds tab widths while hovered");
  Expect(third.Find(UiSelector::Key("close-tab")).One().bounds.Contains(close_position),
         "the successor's close button moves under the stationary pointer");
  mouse_click(close_position);
  Expect(!ui.Find(UiSelector::Key(neighbor_id + 2)).Exists() && has_original_width(fourth),
         "another click at the same point closes the successor with widths held");
  hover_at({close_position.x, 100.0F});
  Expect(fourth.One().bounds.width > original_width, "leaving the strip restores fitted widths");

  add_tab.Tap();
  add_tab.Tap();
  ui.Pump(std::chrono::seconds(1));
  const auto rightmost = ui.Find(UiSelector::Key(neighbor_id + 7)).Find(UiSelector::Key("tab-chip"));
  const Point fourth_close = center(fourth.Find(UiSelector::Key("close-tab")).One().bounds);
  hover_at(fourth_close);
  mouse_click(fourth_close);
  Expect(!ui.Find(UiSelector::Key(neighbor_id + 3)).Exists(), "another middle tab closes after adding tabs");
  Expect(has_original_width(rightmost), "closing a middle tab starts a new width hold");
  const Point last_close = center(rightmost.Find(UiSelector::Key("close-tab")).One().bounds);
  hover_at(last_close);
  mouse_click(last_close);
  Expect(!ui.Find(UiSelector::Key(neighbor_id + 7)).Exists() && neighbor.One().bounds.width > original_width,
         "closing the rightmost tab releases widths without waiting for pointer exit");
  Expect(CapturePaint(ui).find("alpha=0.200000") == std::string::npos,
         "closing a tab does not paint the old bright exit highlight");
}

void ExpectDenseAndDetachedTabLayout() {
  constexpr int tab_count = 20;
  hoku::BrowserSession session;
  const auto first = session.ActiveTab();
  for (int index = 1; index < tab_count; ++index) {
    session.NewTab();
  }
  const auto last = session.ActiveTab();
  const Application application{[] { return DragTestStrip(tab_count, true); }, {.show_debug_overlay = false}};
  UiTest ui(application, {
      .viewport = {800.0F, 400.0F},
      .resource_provider = std::make_shared<PackageResources>(HOKU_UI_TEST_PACKAGE),
  });
  ui.Pump(std::chrono::seconds(1));
  ui.Pump(std::chrono::seconds(1));
  const auto first_chip = ui.Find(UiSelector::Key(first)).Find(UiSelector::Key("tab-chip"));
  const auto last_chip = ui.Find(UiSelector::Key(last)).Find(UiSelector::Key("tab-chip"));
  const auto add_tab = ui.Find(UiSelector::Key("new-tab"));
  const Rect first_bounds = first_chip.One().bounds;
  const Rect last_bounds = last_chip.One().bounds;
  const Rect add_bounds = add_tab.One().bounds;
  Expect(first_bounds.x >= 0.0F && add_bounds.x + add_bounds.width <= 800.01F &&
             ui.Find(UiSelector::Key("tab-row")).One().bounds.width <= 800.01F,
         "20 tabs and the add button fit without horizontal scrolling");
  Expect(add_bounds.x + add_bounds.width <= 780.01F, "the add button leaves a trailing window drag area");
  Expect(add_bounds.x - (last_bounds.x + last_bounds.width) > 8.0F,
         "the add button clears the active last tab's curved seam");
  Expect(first_bounds.width < 40.0F && last_chip.Find(UiSelector::Key("close-tab")).One().enabled,
         "the active tab keeps its close button at compact widths");
  const Point first_center{first_bounds.x + first_bounds.width * 0.5F, first_bounds.y + first_bounds.height * 0.5F};
  ui.SendPointer({PointerEventType::Move, 1, first_center});
  ui.Pump();
  ui.TapAt(first_center, {.device_kind = PointerDeviceKind::Mouse});
  Expect(first_chip.Find(UiSelector::Key("close-tab")).One().enabled, "clicking a compact favicon selects its tab");

  ui.PressKey(Key::F6);
  const Rect detached = first_chip.One().bounds;
  Expect(std::abs(detached.x - (first_bounds.x + 230.0F)) < 0.01F && std::abs(detached.width - 96.0F) < 0.01F,
         "a detached tab retains its horizontal position and width while held");
  ui.PressKey(Key::F7);
  ui.Pump(std::chrono::seconds(1));
  const Rect restored = first_chip.One().bounds;
  Expect(std::abs(restored.x - first_bounds.x) < 0.01F && restored.width > detached.width,
         "releasing a detached tab restores the leading single-tab layout");
}

} // namespace

int main() {
  try {
    const Application application{
        App,
        {
            .window = {
                .title = "Hoku",
                .initial_size = {1120.0F, 760.0F},
                .minimum_size = Size{640.0F, 480.0F},
                .chrome_mode = WindowChromeMode::Custom,
                .title_bar_height = 42.0F,
            },
            .show_debug_overlay = false,
            .application_hooks = {
                huxerui::InstallWebView,
            },
        },
    };
    UiTest ui(application, {
        .viewport = {1120.0F, 760.0F},
        .resource_provider = std::make_shared<PackageResources>(HOKU_UI_TEST_PACKAGE),
    });

    const UiNodeInfo address = ui.Find(UiSelector::AllOf(
        UiSelector::Type<TextField>(),
        UiSelector::Key("address-field")
    )).One();
    Expect(address.in_viewport, "address field visible");
    Expect(address.size.height >= 40.0F && address.size.height <= 48.0F, "address field height");
    Expect(address.size.width > 400.0F, "address field expanded width");
    Expect(!ui.Find(UiSelector::Text("Address and search")).Exists(), "visual address label hidden");
    Expect(!ui.Find(UiSelector::Text("Go")).Exists(), "go button removed");
    Expect(ui.Find(UiSelector::Key("browser-menu")).Exists(), "browser menu present");
    const auto web_view = ui.Find(UiSelector::Key("tab-web-view"));
    const UiNodeInfo startup_web_view = web_view.One();
    Expect(startup_web_view.type == "PlatformView" && startup_web_view.in_viewport && startup_web_view.enabled,
           "new tab already displays a WebView");
    Expect(address.value == "", "new tab address is empty");
    const std::string toolbar_snapshot = ui.CaptureSnapshot().ToString();
    const auto platform_view_position = toolbar_snapshot.find("platform-view");
    Expect(platform_view_position != std::string::npos, "startup frame retains the native WebView placement");
    const auto platform_view_opacity = toolbar_snapshot.rfind("opacity=", platform_view_position);
    Expect(platform_view_opacity != std::string::npos &&
               std::string_view(toolbar_snapshot).substr(platform_view_opacity).starts_with("opacity=1.000000"),
           "startup leaves the native surface visible with its own background");
    Expect(toolbar_snapshot.find("corner_radius=20.000000") != std::string::npos,
           "capsule address outline");

    ui.Find(UiSelector::Key("browser-menu")).Tap();
    auto settings_item = ui.FindSemantics(UiSemanticSelector::AllOf(
        UiSemanticSelector::Role(SemanticRole::MenuItem),
        UiSemanticSelector::Label("Settings")
    ));
    Expect(settings_item.Exists(), "settings menu item");
    settings_item.PerformSemanticAction({SemanticActionKind::Activate, {}});

    Expect(ui.Find(UiSelector::Key("settings-page")).Exists(), "settings page opened");
    Expect(ui.Find(UiSelector::Key("settings-nav")).Exists(), "settings navigation");
    Expect(ui.Find(UiSelector::Key("settings-title")).Exists(), "settings content title");
    Expect(ui.Find(UiSelector::Key("appearance-card")).Exists(), "appearance card");
    const auto close_position_setting = ui.FindSemantics(UiSemanticSelector::Label("Keep tab close buttons in place"));
    Expect(close_position_setting.One().checked == SemanticCheckedState::Checked, "tab close position is held by default");
    close_position_setting.PerformSemanticAction({SemanticActionKind::Activate, {}});
    Expect(close_position_setting.One().checked == SemanticCheckedState::Unchecked, "close position setting can be disabled");
    close_position_setting.PerformSemanticAction({SemanticActionKind::Activate, {}});
    const UiNodeQuery settings_address_query = ui.Find(UiSelector::Key("settings-toolbar"))
        .Find(UiSelector::AllOf(
            UiSelector::Type<TextField>(),
            UiSelector::Key("address-field")
        ));
    const UiNodeInfo settings_address = settings_address_query.One();
    Expect(settings_address.value == hoku::kSettingsUrl, "settings internal URL");
    Expect(settings_address.enabled, "settings address remains visible");
    settings_address_query.Tap();
    Expect(!ui.ActiveTextInput().has_value(), "settings address read only");

    const UiNodeInfo navigation = ui.Find(UiSelector::Key("settings-nav")).One();
    const UiNodeInfo card = ui.Find(UiSelector::Key("appearance-card")).One();
    const UiNodeInfo title = ui.Find(UiSelector::Key("settings-title")).One();
    Expect(navigation.in_viewport && card.in_viewport && title.in_viewport, "settings layout visible");
    Expect(navigation.bounds.x < card.bounds.x, "navigation left of content");
    Expect(card.bounds.x >= 208.0F, "content outside navigation pane");
    Expect(card.size.width >= 600.0F && card.size.width <= 720.0F, "content width bounded");
    Expect(title.bounds.y < card.bounds.y, "title above appearance card");

    ui.SetWindowMetrics({.viewport = {640.0F, 480.0F}});
    ui.Pump();
    const UiNodeInfo narrow_card = ui.Find(UiSelector::Key("appearance-card")).One();
    Expect(narrow_card.in_viewport, "narrow settings card visible");
    Expect(narrow_card.bounds.x >= 208.0F, "narrow card remains after navigation");
    Expect(narrow_card.bounds.x + narrow_card.bounds.width <= 640.0F, "narrow card fits viewport");

    ui.Find(UiSelector::Text("Dark")).Tap();
    ui.Pump(std::chrono::seconds(1));
    ExpectSelected(ui, "Dark");
    ui.Find(UiSelector::Text("Light")).Tap();
    ui.Pump(std::chrono::seconds(1));
    ExpectSelected(ui, "Light");
    ui.Find(UiSelector::Text("Follow system")).Tap();
    ui.Pump(std::chrono::seconds(1));
    ExpectSelected(ui, "Follow system");

    const auto search_button = ui.Find(UiSelector::Key("tab-search-button"));
    const auto search_panel = ui.Find(UiSelector::Key("tab-search-panel"));
    const auto search_field = ui.Find(UiSelector::Key("tab-search-field"));
    search_button.Tap();
    Expect(search_panel.Find(UiSelector::Text("Settings")).Exists(), "tab search lists settings");
    search_field.EnterText("NEW TAB");
    Expect(!search_panel.Find(UiSelector::Text("Settings")).Exists(), "tab search filters by title");
    search_panel.Find(UiSelector::Text("New tab")).Tap();
    ui.Pump(std::chrono::seconds(1));
    Expect(!search_panel.Exists(), "tab selection dismisses search");
    Expect(web_view.One().in_viewport, "existing browser tab selected");

    search_button.Tap();
    search_field.EnterText("no results");
    Expect(search_panel.Find(UiSelector::Text("No matching tabs")).Exists(), "tab search empty result");
    ui.PressKey(Key::Escape);
    ui.Pump(std::chrono::seconds(1));
    Expect(!search_panel.Exists(), "escape dismisses tab search");

    search_button.Tap();
    search_field.EnterText("HOKU://SET");
    ui.PressKey(Key::Enter);
    ui.Pump(std::chrono::seconds(1));
    Expect(!search_panel.Exists(), "enter selects and dismisses tab search");
    Expect(ui.Find(UiSelector::Key("settings-page")).One().in_viewport, "search by address selects retained settings tab");

    search_button.Tap();
    search_panel.Find(UiSelector::Text("New tab")).Tap();
    ui.Pump(std::chrono::seconds(1));
    const auto browser_address = ui.Find(UiSelector::Key("browser-toolbar-1"))
        .Find(UiSelector::Key("address-field"));
    const Rect before_navigation = web_view.One().bounds;
    browser_address.Tap();
    browser_address.EnterText("https://example.com/");
    ui.PressKey(Key::Enter);
    ui.Pump();
    Expect(browser_address.One().value == "https://example.com/", "address submitted");
    Expect(web_view.One().bounds == before_navigation, "navigation preserves the WebView layout");
    Expect(web_view.One().focused && !ui.ActiveTextInput().has_value(), "address submission hands keyboard focus to the page");

    const Size maximized_viewport{1707.0F, 1019.0F};
    ui.SetWindowMetrics({.viewport = maximized_viewport});
    ui.Pump();
    ui.Find(UiSelector::Key("new-tab")).Tap();
    const auto new_page = ui.Find(UiSelector::Key("browser-pages")).Find(UiSelector::Key(hoku::TabId{3}));
    const auto new_web_view = new_page.Find(UiSelector::Key("tab-web-view")).One();
    Expect(new_web_view.in_viewport && new_web_view.bounds.x == 0.0F &&
               new_web_view.bounds.width == maximized_viewport.width &&
               new_web_view.bounds.y + new_web_view.bounds.height == maximized_viewport.height,
           "the native node and its startup paint reach the maximized window edges");
    Expect(!new_page.Find(UiSelector::Key("tab-startup-background")).Exists(),
           "the startup background has no independent hit target");
    const auto new_chip = ui.Find(UiSelector::Key("tab-row")).Find(UiSelector::Key(hoku::TabId{3}))
        .Find(UiSelector::Key("tab-chip"));
    const Rect entering_chip = new_chip.One().bounds;
    ui.Pump();
    ui.Pump(std::chrono::seconds(1));
    const Rect settled_chip = new_chip.One().bounds;
    Expect(entering_chip.x > settled_chip.x && entering_chip.y == settled_chip.y,
           "a new tab slides in horizontally from the right");

    const auto tab_row = ui.Find(UiSelector::Key("tab-row"));
    const auto shortcut_tab = tab_row.Find(UiSelector::Key(hoku::TabId{4}));
    ui.PressKey(Key::T, {.control = true});
    Expect(shortcut_tab.Exists(), "Ctrl+T opens a blank tab");
    ui.PressKey(Key::W, {.control = true});
    Expect(!shortcut_tab.Exists(), "Ctrl+W closes the current tab");

    const Point middle{settled_chip.x + settled_chip.width * 0.5F, settled_chip.y + settled_chip.height * 0.5F};
    ui.SendPointer({PointerEventType::Down, 1, middle, PointerDeviceKind::Mouse,
                    PointerButton::Middle, PointerButton::Middle});
    ui.Pump();
    Expect(!tab_row.Find(UiSelector::Key(hoku::TabId{3})).Exists(), "middle button down closes its tab");
    ui.SendPointer({PointerEventType::Up, 1, middle, PointerDeviceKind::Mouse, PointerButton::Middle});
    ui.Pump();

    const auto settings_nav = ui.Find(UiSelector::Key("settings-nav"));
    settings_nav.Find(UiSelector::Text("Keyboard shortcuts")).Tap();
    Expect(ui.Find(UiSelector::Key("shortcut-card")).Exists() && !ui.Find(UiSelector::Key("appearance-card")).Exists(),
           "the shortcuts section shows its own settings");
    ui.Find(UiSelector::Key("edit-shortcut-0")).Tap();
    ui.PressKey(Key::Y, {.control = true});
    Expect(ui.FindSemantics(UiSemanticSelector::Label("Ctrl+Y")).Exists(), "settings display the recorded shortcut");
    ui.PressKey(Key::T, {.control = true});
    const auto remapped_tab = tab_row.Find(UiSelector::Key(hoku::TabId{5}));
    Expect(!remapped_tab.Exists(), "the old binding is released after recording");
    ui.PressKey(Key::Y, {.control = true});
    Expect(remapped_tab.Exists(), "the recorded shortcut takes effect");
    ui.PressKey(Key::W, {.control = true});
    settings_nav.Find(UiSelector::Text("Language")).Tap();
    Expect(ui.Find(UiSelector::Key("language-card")).Exists() && !ui.Find(UiSelector::Key("shortcut-card")).Exists(),
           "the language section shows its own settings");
    ui.FindSemantics(UiSemanticSelector::Label("简体中文")).PerformSemanticAction({SemanticActionKind::Activate, {}});
    ui.Pump();
    Expect(ui.Find(UiSelector::Key("settings-title")).One().text == "语言", "settings language changes to Simplified Chinese");

    ExpectDragSurvivesTabTransfer();
    ExpectDenseAndDetachedTabLayout();

    std::cout << "Hoku UI tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
