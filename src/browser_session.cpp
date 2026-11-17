#include "browser_session.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace hoku {

BrowserSession::BrowserSession() {
  NewTab();
}

BrowserSession::BrowserSession(TabInfo tab, std::shared_ptr<TabId> next_id)
    : active_id_(tab.id), next_id_(std::move(next_id)) {
  tabs_.push_back(std::move(tab));
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

const TabInfo* BrowserSession::FindTab(TabId id) const noexcept {
  const auto tab = std::ranges::find(tabs_, id, &TabInfo::id);
  return tab == tabs_.end() ? nullptr : &*tab;
}

BrowserSession BrowserSession::WindowWithTab(TabInfo tab) const {
  return BrowserSession(std::move(tab), next_id_);
}

TabId BrowserSession::NewTab(std::string url) {
  const TabId id = (*next_id_)++;
  tabs_.push_back(TabInfo{.id = id, .title = url.empty() ? "New tab" : url, .url = std::move(url)});
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

  const TabId id = (*next_id_)++;
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

bool BrowserSession::InsertTab(TabInfo tab, std::optional<TabId> before) {
  const auto insertion = before ? std::ranges::find(tabs_, *before, &TabInfo::id) : tabs_.end();
  if ((before && insertion == tabs_.end()) || FindTab(tab.id)) {
    return false;
  }
  const TabId id = tab.id;
  tabs_.insert(insertion, std::move(tab));
  active_id_ = id;
  return true;
}

bool BrowserSession::ReorderTab(TabId id, std::optional<TabId> before) {
  const auto tab = std::ranges::find(tabs_, id, &TabInfo::id);
  const auto insertion = before ? std::ranges::find(tabs_, *before, &TabInfo::id) : tabs_.end();
  if (tab == tabs_.end() || (before && insertion == tabs_.end()) ||
      tab == insertion || std::next(tab) == insertion) {
    return false;
  }
  if (tab < insertion) {
    std::rotate(tab, std::next(tab), insertion);
  } else {
    std::rotate(insertion, tab, std::next(tab));
  }
  active_id_ = id;
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
