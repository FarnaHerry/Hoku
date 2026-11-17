#include "browser_session.h"
#include "destination.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void ExpectEqual(std::string_view actual, std::string_view expected, std::string_view name) {
  if (actual != expected) {
    throw std::runtime_error(std::string(name) + ": expected '" + std::string(expected) +
                             "', got '" + std::string(actual) + "'");
  }
}

void Expect(bool condition, std::string_view name) {
  if (!condition) {
    throw std::runtime_error(std::string(name) + ": condition failed");
  }
}

void TestDestinations() {
  using hoku::ResolveDestination;
  ExpectEqual(ResolveDestination("  https://example.com/a?x=1  "),
              "https://example.com/a?x=1", "trimmed URL");
  ExpectEqual(ResolveDestination("example.com/docs"), "https://example.com/docs", "domain");
  ExpectEqual(ResolveDestination("example.com:8080/docs"), "https://example.com:8080/docs",
              "domain port");
  ExpectEqual(ResolveDestination("intranet:8080"), "https://intranet:8080", "intranet port");
  ExpectEqual(ResolveDestination("localhost:3000"), "http://localhost:3000", "localhost");
  ExpectEqual(ResolveDestination("127.0.0.1:8000"), "http://127.0.0.1:8000", "loopback IP");
  ExpectEqual(ResolveDestination("mailto:user@example.com"), "mailto:user@example.com",
              "existing scheme");
  ExpectEqual(ResolveDestination("//example.com"), "https://example.com", "relative scheme");
  ExpectEqual(ResolveDestination("C++ tutorial"),
              "https://www.google.com/search?q=C%2B%2B%20tutorial", "encoded search");
  ExpectEqual(ResolveDestination("中文 搜索"),
              "https://www.google.com/search?q=%E4%B8%AD%E6%96%87%20%E6%90%9C%E7%B4%A2",
              "UTF-8 search");
  ExpectEqual(ResolveDestination(" \t "), "", "empty input");
  using hoku::HttpOrigin;
  ExpectEqual(HttpOrigin("https://example.com/a?q=1"), "https://example.com", "HTTPS origin");
  ExpectEqual(HttpOrigin("http://example.com:8080/path"), "http://example.com:8080", "HTTP origin port");
  ExpectEqual(HttpOrigin("mailto:user@example.com"), "", "non-HTTP origin");
  ExpectEqual(HttpOrigin("https://"), "", "empty HTTP authority");
}

void TestSession() {
  hoku::BrowserSession session;
  Expect(session.Tabs().size() == 1, "initial tab count");
  const hoku::TabId first = session.ActiveTab();
  const hoku::TabId second = session.NewTab();
  const hoku::TabId third = session.NewTab();
  Expect(session.ActiveTab() == third, "new tab becomes active");

  Expect(session.SelectTab(second), "select existing tab");
  Expect(!session.SelectTab(second), "reselect is a no-op");
  Expect(session.CloseTab(second), "close active middle tab");
  Expect(session.ActiveTab() == third, "next tab inherits active state");
  Expect(session.Tabs().size() == 2, "middle tab removed");

  Expect(session.CloseTab(third), "close active last tab");
  Expect(session.ActiveTab() == first, "previous tab inherits active state");
  Expect(!session.CloseTab(third), "missing close is a no-op");

  Expect(session.UpdateTab(first, "https://example.com", "Example"), "update metadata");
  Expect(!session.UpdateTab(first, "https://example.com", "Example"), "metadata no-op");
  Expect(session.Tabs().front().title == "Example", "page title used");
  Expect(session.UpdateTab(first, "about:blank", ""), "fallback title changed");
  Expect(session.Tabs().front().title == "about:blank", "URL fallback title");

  const hoku::TabId settings = session.OpenSettings();
  Expect(session.ActiveTab() == settings, "settings tab becomes active");
  Expect(session.Tabs().size() == 2, "settings tab added");
  Expect(session.Tabs().back().kind == hoku::TabKind::Settings, "settings tab kind");
  Expect(session.Tabs().back().title == "Settings", "settings tab title");
  Expect(session.Tabs().back().url == hoku::kSettingsUrl, "settings tab URL");
  Expect(session.OpenSettings() == settings, "settings tab reused");
  Expect(session.Tabs().size() == 2, "settings tab deduplicated");
  Expect(session.CloseTab(settings), "close settings tab");
  Expect(session.ActiveTab() == first, "web tab resumes after settings closes");

  Expect(session.CloseTab(first), "close final tab");
  Expect(session.Tabs().size() == 1, "closing final tab creates replacement");
  Expect(session.Tabs().front().id != first, "replacement has fresh identity");
  Expect(session.ActiveTab() == session.Tabs().front().id, "replacement is active");
  Expect(session.Tabs().front().url.empty(), "replacement starts empty");

  const hoku::TabId opener = session.ActiveTab();
  const hoku::TabId linked = session.NewTab("https://example.com/linked");
  Expect(session.ActiveTab() == linked && session.Tabs().size() == 2, "link opens an active new tab");
  Expect(session.Tabs().front().id == opener && session.Tabs().front().url.empty(), "opener is retained");
  ExpectEqual(session.Tabs().back().url, "https://example.com/linked", "link sets the new tab destination");

  hoku::BrowserSession detached = session.WindowWithTab(*session.FindTab(linked));
  Expect(session.CloseTab(linked), "detach removes tab from its original strip");
  const hoku::TabId source_new = session.NewTab();
  const hoku::TabId detached_new = detached.NewTab();
  Expect(source_new != detached_new, "windows allocate distinct tab IDs");
  Expect(!session.InsertTab(*detached.FindTab(linked), detached_new), "stale drop anchor leaves tabs in place");
  Expect(session.InsertTab(*detached.FindTab(linked), opener), "merge inserts before the target tab");
  Expect(detached.CloseTab(linked), "merge removes tab from the detached strip");
  Expect(session.Tabs().front().id == linked && session.ActiveTab() == linked, "merge preserves and activates the moved tab");
  ExpectEqual(session.Tabs().front().url, "https://example.com/linked", "merge preserves the URL");
  Expect(!session.InsertTab(*session.FindTab(linked)), "a repeated drop cannot duplicate the tab");
  Expect(session.ReorderTab(linked, std::nullopt), "same-window drop can move a tab to the end");
  Expect(session.Tabs().back().id == linked, "reorder preserves the tab ID");
}

} // namespace

int main() {
  try {
    TestDestinations();
    TestSession();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Hoku core tests passed\n";
  return 0;
}
