#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hoku {

using TabId = std::uint64_t;

struct TabInfo {
  TabId id = 0;
  std::string title = "New tab";
  std::string url;
};

class BrowserSession {
public:
  BrowserSession();

  [[nodiscard]] const std::vector<TabInfo>& Tabs() const noexcept;
  [[nodiscard]] TabId ActiveTab() const noexcept;
  [[nodiscard]] std::size_t ActiveIndex() const noexcept;

  TabId NewTab();
  bool SelectTab(TabId id);
  bool CloseTab(TabId id);
  bool UpdateTab(TabId id, std::string url, std::string title);

private:
  std::vector<TabInfo> tabs_;
  TabId active_id_ = 0;
  TabId next_id_ = 1;
};

} // namespace hoku
