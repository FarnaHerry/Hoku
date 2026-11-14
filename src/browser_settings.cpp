#include "browser_settings.h"

#include <chrono>
#include <cstddef>

#include <app_resources.h>
#include <huxerui/huxerui.h>

namespace hoku {
namespace {

using namespace huxerui;

constexpr float kSettingsNavigationWidth = 208.0F;
constexpr float kSettingsContentMaxWidth = 720.0F;
constexpr float kSettingsPagePadding = 32.0F;
constexpr float kSettingsCardPadding = 24.0F;
constexpr float kSettingsIconSize = 20.0F;
constexpr float kSettingsCornerRadius = 12.0F;

} // namespace

[[huxerui::composable]]
View SettingsContent(State<std::optional<ThemeBrightness>> theme_override) {
  const ThemeSpec& theme = UseTheme();
  const auto& colors = theme.colors;
  const bool dark = theme.brightness == ThemeBrightness::Dark;
  const bool system_dark = UseSystemThemeBrightness() == ThemeBrightness::Dark;
  const std::optional<ThemeBrightness>& selected = theme_override.Get();
  const std::size_t selected_index = !selected ? 0 : (*selected == ThemeBrightness::Dark ? 2 : 1);
  auto scene_transition = UseSceneTransition();
  auto tasks = UseTaskScope();
  auto animating = UseState(false);
  const auto apply_theme = [theme_override, scene_transition, tasks, animating, dark, system_dark,
                             motion = theme.motion](std::size_t index) {
    if (animating.Get()) {
      return;
    }

    const std::optional<ThemeBrightness> next = index == 0
                                                    ? std::nullopt
                                                    : std::optional<ThemeBrightness>{
                                                          index == 2 ? ThemeBrightness::Dark
                                                                     : ThemeBrightness::Light,
                                                      };
    const bool target_dark = index == 2 || (index == 0 && system_dark);
    const auto mutation = [theme_override, next] {
      theme_override = next;
    };
    if (dark == target_dark || motion.reduced_motion) {
      mutation();
      return;
    }

    animating = true;
    tasks.Launch([animating, duration = motion.slow + motion.fast]() -> Task<void> {
      co_await Delay(std::chrono::duration<double>{duration});
      animating = false;
    });
    const TransitionSpec reveal{
        CircularRevealTransition{},
        TweenSpec{motion.slow},
    };
    scene_transition.RunFromCurrentInteraction(
        dark ? reveal.Reversed() : reveal,
        mutation
    );
  };

  return Row {
    Column {
      Row {
        Image(app::images::settings)
            .With(Frame{.width = kSettingsIconSize, .height = kSettingsIconSize}),
        Text("Settings", TextRole::Title),
      }.With(Spacing(12.0F), CrossAlign(CrossAxisAlignment::Center)),
      NavigationPane(
          {NavigationItem(dark ? app::images::sun : app::images::moon, "Appearance")},
          0,
          true
      ).Key("settings-appearance-nav"),
    }.With(
        Frame{.width = kSettingsNavigationWidth},
        Padding(EdgeInsets::All(20.0F)),
        Spacing(20.0F),
        Background(colors.surface_container_low)
    ),
    ScrollView(
        Row {
          Column {
            Text("Appearance", TextRole::Title).Key("settings-title"),
            Column {
              Text("Theme", TextRole::Body).With(FontSize(18.0F)),
              Text("Choose how Hoku and supported web pages look.", TextRole::Body)
                  .With(Foreground(colors.on_surface_variant)),
              SegmentedButton(
                  {"Follow system", "Light", "Dark"},
                  selected_index
              )
                  .OnChanged(apply_theme)
                  .Key("theme-mode"),
            }.With(
                Padding(EdgeInsets::All(kSettingsCardPadding)),
                Spacing(12.0F),
                CornerRadius(kSettingsCornerRadius),
                Background(colors.surface_container),
                ClipChildren()
            ).Key("appearance-card"),
          }.With(
              Frame{.width = kSettingsContentMaxWidth},
              Padding(EdgeInsets::All(kSettingsPagePadding)),
              Spacing(16.0F),
              CrossAlign(CrossAxisAlignment::Stretch)
          ),
        }.With(
            MainAlign(MainAxisAlignment::Center),
            CrossAlign(CrossAxisAlignment::Stretch)
        )
    ).With(Grow()),
  }.With(
      Grow(),
      CrossAlign(CrossAxisAlignment::Stretch),
      Background(colors.background)
  ).Key("settings-page");
}

} // namespace hoku
