#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <huxerui/event.h>

namespace hoku {

enum class BrowserAction : std::size_t { NewTab, CloseTab, Fullscreen };

struct BrowserShortcut {
  huxerui::Key key;
  huxerui::KeyModifiers modifiers;
  bool operator==(const BrowserShortcut&) const = default;
};

struct BrowserPreferences {
  std::size_t language = 0;
  std::array<BrowserShortcut, 3> shortcuts{{
      {huxerui::Key::T, {.control = true}},
      {huxerui::Key::W, {.control = true}},
      {huxerui::Key::F11, {}},
  }};
  bool operator==(const BrowserPreferences&) const = default;
};

inline bool IsShortcutKey(huxerui::Key key) {
  using huxerui::Key;
  return (key >= Key::A && key <= Key::Z) || (key >= Key::Digit0 && key <= Key::Digit9) ||
         (key >= Key::F1 && key <= Key::F24);
}

inline std::string ShortcutLabel(const BrowserShortcut& shortcut) {
  using huxerui::Key;
  std::string label;
  if (shortcut.modifiers.control) label += "Ctrl+";
  if (shortcut.modifiers.alt) label += "Alt+";
  if (shortcut.modifiers.shift) label += "Shift+";
  if (shortcut.modifiers.meta) label += "Meta+";
  const auto key = static_cast<int>(shortcut.key);
  if (shortcut.key >= Key::A && shortcut.key <= Key::Z) {
    label += static_cast<char>('A' + key - static_cast<int>(Key::A));
  } else if (shortcut.key >= Key::Digit0 && shortcut.key <= Key::Digit9) {
    label += static_cast<char>('0' + key - static_cast<int>(Key::Digit0));
  } else {
    label += 'F';
    label += std::to_string(1 + key - static_cast<int>(Key::F1));
  }
  return label;
}

} // namespace hoku
