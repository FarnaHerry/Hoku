#include "browser_session.h"

#include <algorithm>
#include <utility>

namespace hoku {

BrowserSession::BrowserSession() {
  NewTab();
}

const std::vector<TabInfo>& BrowserSession::Tabs() const noexcept {
  return tabs_;
}

TabId BrowserSession::ActiveTab() const noexcept {
  return active_id_;
}

std::size_t BrowserSession::ActiveIndex() const noexcept {
  const auto active = std::ranges::find_if(tabs_, [this](const TabInfo& tab) {
    return tab.id == active_id_;
  });
  return active == tabs_.end()
             ? 0
             : static_cast<std::size_t>(std::distance(tabs_.begin(), active));
}

TabId BrowserSession::NewTab() {
  const TabId id = next_id_++;
  tabs_.push_back(TabInfo{.id = id});
  active_id_ = id;
  return id;
}

TabId BrowserSession::OpenSettings() {
  const auto existing = std::ranges::find_if(tabs_, [](const TabInfo& tab) {
    return tab.kind == TabKind::Settings;
  });
  if (existing != tabs_.end()) {
    active_id_ = existing->id;
    return existing->id;
  }

  const TabId id = next_id_++;
  tabs_.push_back(TabInfo{
      .id = id,
      .kind = TabKind::Settings,
      .title = "Settings",
      .url = kSettingsUrl,
  });
  active_id_ = id;
  return id;
}

bool BrowserSession::SelectTab(TabId id) {
  const auto selected = std::ranges::find_if(tabs_, [id](const TabInfo& tab) {
    return tab.id == id;
  });
  if (selected == tabs_.end() || active_id_ == id) {
    return false;
  }
  active_id_ = id;
  return true;
}

bool BrowserSession::CloseTab(TabId id) {
  const auto closing = std::ranges::find_if(tabs_, [id](const TabInfo& tab) {
    return tab.id == id;
  });
  if (closing == tabs_.end()) {
    return false;
  }

  const auto closing_index = static_cast<std::size_t>(std::distance(tabs_.begin(), closing));
  const bool was_active = active_id_ == id;
  tabs_.erase(closing);
  if (tabs_.empty()) {
    NewTab();
    return true;
  }
  if (was_active) {
    active_id_ = tabs_[std::min(closing_index, tabs_.size() - 1)].id;
  }
  return true;
}

bool BrowserSession::UpdateTab(TabId id, std::string url, std::string title) {
  const auto selected = std::ranges::find_if(tabs_, [id](const TabInfo& tab) {
    return tab.id == id;
  });
  if (selected == tabs_.end()) {
    return false;
  }

  if (title.empty()) {
    title = url.empty() ? "New tab" : url;
  }
  if (selected->url == url && selected->title == title) {
    return false;
  }
  selected->url = std::move(url);
  selected->title = std::move(title);
  return true;
}

} // namespace hoku
