#include "browser_settings.h"

#include <algorithm>
#include <array>
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

View SettingsCard(View content, Color background) {
  return std::move(content).With(Padding(EdgeInsets::All(kSettingsCardPadding)), Spacing(12.0F),
                                CrossAlign(CrossAxisAlignment::Stretch), CornerRadius(kSettingsCornerRadius),
                                Background(background));
}

[[huxerui::composable]]
View ShortcutEditor(State<BrowserPreferences> preferences, std::size_t index, StringVariant label) {
  auto recording = UseState(false);
  auto error = UseState(std::optional<StringResource>{});
  const auto button = UseViewHandle();
  const BrowserShortcut shortcut = preferences->shortcuts[index];
  return Column {
    Row {
      Text(std::move(label)).With(Grow()),
      Button(recording.Get() ? StringVariant(app::strings::shortcut_record) : StringVariant(ShortcutLabel(shortcut)))
          .With(button)
          .OnClick([recording, error, button] {
            error = std::nullopt;
            recording = true;
            button.RequestFocus();
          })
          .On<ViewEvents::FocusChanged>([recording](bool focused) {
            if (!focused) recording = false;
          })
          .On<ViewEvents::KeyIntercept>([recording, error, preferences, index](const KeyEvent& event) {
            if (!recording.Get()) return false;
            if (event.type != KeyEventType::Down || event.repeat) return true;
            if (event.key == Key::Escape) {
              recording = false;
              error = std::nullopt;
              return true;
            }
            if (event.key >= Key::ShiftLeft && event.key <= Key::MetaRight) return true;
            const auto& mods = event.modifiers;
            if (!IsShortcutKey(event.key) || (event.key < Key::F1 && !mods.control && !mods.alt && !mods.meta)) {
              error = app::strings::shortcut_invalid;
              return true;
            }
            const BrowserShortcut next{event.key, mods};
            const auto& shortcuts = preferences->shortcuts;
            if (std::ranges::find(shortcuts, next) != shortcuts.end() && shortcuts[index] != next) {
              error = app::strings::shortcut_conflict;
              return true;
            }
            preferences.Update([index, next](BrowserPreferences& value) { value.shortcuts[index] = next; });
            error = std::nullopt;
            recording = false;
            return true;
          })
          .Key("edit-shortcut-" + std::to_string(index)),
    }.With(Spacing(12.0F), CrossAlign(CrossAxisAlignment::Center)),
    error.Get() ? Text(*error.Get()).With(Foreground(UseTheme().colors.error)) : View{},
  }.With(Spacing(4.0F), CrossAlign(CrossAxisAlignment::Stretch));
}

} // namespace

[[huxerui::composable]]
View SettingsContent(State<std::optional<ThemeBrightness>> theme_override, State<bool> keep_tab_close_position,
                     State<BrowserPreferences> preferences) {
  const ThemeSpec& theme = UseTheme();
  const auto& colors = theme.colors;
  const bool dark = theme.brightness == ThemeBrightness::Dark;
  const bool system_dark = UseSystemThemeBrightness() == ThemeBrightness::Dark;
  const std::optional<ThemeBrightness>& selected = theme_override.Get();
  const std::size_t selected_index = !selected ? 0 : (*selected == ThemeBrightness::Dark ? 2 : 1);
  auto scene_transition = UseSceneTransition();
  auto tasks = UseTaskScope();
  auto animating = UseState(false);
  auto section = UseState(std::size_t{0});
  const std::array sections{app::strings::appearance, app::strings::shortcuts, app::strings::language};
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

  Views content;
  content.Add(Text(sections[section.Get()], TextRole::Title).Key("settings-title"));
  if (section.Get() == 0) {
    content.Add(SettingsCard(Column {
      Text(app::strings::theme, TextRole::Body).With(FontSize(18.0F)),
      Text(app::strings::theme_help).With(Foreground(colors.on_surface_variant)),
      SegmentedButton({app::strings::follow_system, app::strings::light, app::strings::dark}, selected_index)
          .OnChanged(apply_theme).Key("theme-mode"),
    }, colors.surface_container).With(ClipChildren()).Key("appearance-card"));
    content.Add(SettingsCard(Column {
      Text(app::strings::tabs, TextRole::Body).With(FontSize(18.0F)),
      Switch(app::strings::keep_tab_close_position, keep_tab_close_position)
          .OnChanged([keep_tab_close_position](bool enabled) { keep_tab_close_position = enabled; })
          .Key("keep-tab-close-position"),
    }, colors.surface_container));
  } else if (section.Get() == 1) {
    content.Add(SettingsCard(Column {
      Text(app::strings::shortcut_help).With(Foreground(colors.on_surface_variant)),
      ShortcutEditor(preferences, 0, app::strings::shortcut_new_tab),
      ShortcutEditor(preferences, 1, app::strings::shortcut_close_tab),
      ShortcutEditor(preferences, 2, app::strings::shortcut_fullscreen),
      Button(app::strings::shortcut_reset).OnClick([preferences] {
        preferences.Update([](BrowserPreferences& value) { value.shortcuts = BrowserPreferences{}.shortcuts; });
      }),
    }, colors.surface_container).Key("shortcut-card"));
  } else {
    content.Add(SettingsCard(Column {
      SegmentedButton({app::strings::system_language, app::strings::language_english,
                       app::strings::language_chinese}, preferences->language)
          .OnChanged([preferences](std::size_t index) {
            preferences.Update([index](BrowserPreferences& value) { value.language = index; });
          }).Key("language-mode"),
    }, colors.surface_container).Key("language-card"));
  }

  return Row {
    Column {
      Row {
        Image(app::images::settings)
            .With(Frame{.width = kSettingsIconSize, .height = kSettingsIconSize}),
        Text(app::strings::settings, TextRole::Title),
      }.With(Spacing(12.0F), CrossAlign(CrossAxisAlignment::Center)),
      NavigationPane(
          {NavigationItem(sections[0]), NavigationItem(sections[1]), NavigationItem(sections[2])},
          section.Get(),
          true
      ).OnChanged([section](std::size_t index) { section = index; }).Key("settings-nav"),
    }.With(
        Frame{.width = kSettingsNavigationWidth},
        Padding(EdgeInsets::All(20.0F)),
        Spacing(20.0F),
        Background(colors.surface_container_low)
    ),
    ScrollView(
        Row {
          Column(std::move(content)).With(
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
