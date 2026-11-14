#include <huxerui/huxerui.h>
#include <huxerui/testing/ui_test.h>
#include <huxerui/webview.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "browser_session.h"

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
                .title_bar_height = 52.0F,
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
    const std::string toolbar_snapshot = ui.CaptureSnapshot().ToString();
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
    Expect(ui.Find(UiSelector::Key("settings-appearance-nav")).Exists(), "settings navigation");
    Expect(ui.Find(UiSelector::Key("settings-title")).Exists(), "settings content title");
    Expect(ui.Find(UiSelector::Key("appearance-card")).Exists(), "appearance card");
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

    const UiNodeInfo navigation = ui.Find(UiSelector::Key("settings-appearance-nav")).One();
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

    std::cout << "Hoku UI tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
