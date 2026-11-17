#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hoku {

using TabId = std::uint64_t;

enum class TabKind {
  Web,
  Settings,
};

inline constexpr char kSettingsUrl[] = "hoku://settings";

struct TabInfo {
  TabId id = 0;
  TabKind kind = TabKind::Web;
  std::string title = "New tab";
  std::string url;
};

class BrowserSession {
public:
  BrowserSession();

  [[nodiscard]] const std::vector<TabInfo>& Tabs() const noexcept;
  [[nodiscard]] TabId ActiveTab() const noexcept;
  [[nodiscard]] std::size_t ActiveIndex() const noexcept;
  [[nodiscard]] const TabInfo* FindTab(TabId id) const noexcept;
  [[nodiscard]] BrowserSession WindowWithTab(TabInfo tab) const;

  TabId NewTab(std::string url = {});
  TabId OpenSettings();
  bool SelectTab(TabId id);
  bool CloseTab(TabId id);
  bool InsertTab(TabInfo tab, std::optional<TabId> before = std::nullopt);
  bool ReorderTab(TabId id, std::optional<TabId> before);
  bool UpdateTab(TabId id, std::string url, std::string title);

private:
  BrowserSession(TabInfo tab, std::shared_ptr<TabId> next_id);

  std::vector<TabInfo> tabs_;
  TabId active_id_ = 0;
  std::shared_ptr<TabId> next_id_ = std::make_shared<TabId>(1);
};

} // namespace hoku
