#include "browser_tabs.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <app_resources.h>
#include <huxerui/huxerui.h>

namespace hoku {
namespace {

using namespace huxerui;

constexpr float kPreferredTabWidth = 176.0F;
constexpr float kMinimumTabWidth = 112.0F;
constexpr float kNewTabWidth = 40.0F;
constexpr float kNewTabButtonSize = 32.0F;
constexpr float kTabStripHeight = 40.0F;
constexpr float kTabHeight = 34.0F;
constexpr float kTabIconSize = 16.0F;
constexpr float kTabTitleHeight = 20.0F;
constexpr float kTabTitleAllowance = 80.0F;
constexpr float kTabCloseButtonSize = 22.0F;
constexpr float kTabHorizontalPadding = 8.0F;
constexpr float kTabSpacing = 4.0F;
constexpr float kTabCornerRadius = 10.0F;
constexpr float kTabEntryOffsetX = 12.0F;
constexpr float kTabHoverAlpha = 0.06F;
constexpr float kGeometryTolerance = 0.5F;
constexpr double kTabEntryMotionSeconds = 0.16;
constexpr double kTabCloseMotionSeconds = 0.10;

class MeasureSize {
public:
  using Callback = std::function<void(Size)>;

  explicit MeasureSize(Callback on_size) : on_size_(std::move(on_size)) {}

  class Extension final : public NodeExtension {
  public:
    Extension(ViewNode&, const MeasureSize& spec) : on_size_(spec.on_size_) {}

    void Update(ViewNode&, const MeasureSize& spec) {
      on_size_ = spec.on_size_;
    }

    FrameResult OnFrame(ViewNode& node, const FrameInfo&) override {
      const Rect bounds = node.Bounds();
      if (std::abs(bounds.width - last_.width) <= kGeometryTolerance &&
          std::abs(bounds.height - last_.height) <= kGeometryTolerance) {
        return {};
      }
      last_ = Size{bounds.width, bounds.height};
      if (on_size_) {
        on_size_(last_);
      }
      return {.needs_frame = true};
    }

  private:
    Callback on_size_;
    Size last_{};
  };

private:
  Callback on_size_;
};

std::string_view Utf8Prefix(std::string_view text, std::size_t max_points) {
  std::size_t position = 0;
  for (std::size_t count = 0; position < text.size() && count < max_points; ++count) {
    ++position;
    while (position < text.size() &&
           (static_cast<unsigned char>(text[position]) & 0xC0U) == 0x80U) {
      ++position;
    }
  }
  return text.substr(0, position);
}

[[huxerui::composable]]
View BrowserTabTitle(const TabInfo& tab, float max_width) {
  const ThemeSpec& theme = UseTheme();
  const auto style = detail::DefaultTextStyle(theme, TextRole::Label);
  const TextLayoutOptions no_wrap{.wrap = TextWrap::NoWrap};
  TextMeasurer& measurer = UseTextMeasurer();
  if (measurer.MeasureText(tab.title, style, std::numeric_limits<float>::infinity(), no_wrap)
          .size.width <= max_width) {
    return Text(tab.title, TextRole::Label);
  }

  constexpr std::size_t kSourceLimit = 60;
  std::size_t low = 0;
  std::size_t high = kSourceLimit;
  while (low < high) {
    const std::size_t middle = (low + high + 1) / 2;
    if (measurer.MeasureText(Utf8Prefix(tab.title, middle), style,
                             std::numeric_limits<float>::infinity(), no_wrap)
            .size.width <= max_width) {
      low = middle;
    } else {
      high = middle - 1;
    }
  }
  return Text(std::string(Utf8Prefix(tab.title, low)), TextRole::Label);
}

[[huxerui::composable]]
View BrowserTabChip(
    const TabInfo& tab,
    bool active,
    State<BrowserSession> session,
    State<FaviconMap> favicons,
    std::optional<ImageAsset> favicon,
    float width
) {
  const ThemeSpec& theme = UseTheme();
  auto hovered = UseState(false);
  auto appeared = UseState(theme.motion.reduced_motion);
  Lifecycle([appeared] {
    appeared = true;
  }, 0);

  const TweenSpec entry_motion{
      .duration = theme.motion.reduced_motion ? 0.0 : kTabEntryMotionSeconds,
      .easing = Easing::EaseOut,
  };
  const TweenSpec close_motion{
      .duration = theme.motion.reduced_motion ? 0.0 : kTabCloseMotionSeconds,
      .easing = Easing::EaseOut,
  };
  const bool close_visible = active || hovered.Get();
  Color hover_background = theme.colors.on_surface;
  hover_background.alpha = kTabHoverAlpha;
  const Color background = active ? theme.colors.surface_container_highest
                                  : (hovered.Get() ? hover_background : Color::Transparent());
  const Color foreground = active ? theme.colors.on_surface : theme.colors.on_surface_variant;
  const float title_width = std::max(0.0F, width - kTabTitleAllowance);
  const TabId id = tab.id;

  Image tab_icon = favicon.has_value()
                       ? Image(*favicon)
                       : Image(tab.kind == TabKind::Settings ? app::images::settings : app::images::globe)
                             .Tint(active ? theme.colors.primary : theme.colors.on_surface_variant);

  return Row {
    std::move(tab_icon).With(Frame{.width = kTabIconSize, .height = kTabIconSize}),
    Row {
      BrowserTabTitle(tab, title_width),
    }.With(Grow(), Frame{.height = kTabTitleHeight}, ClipChildren()),
    IconButton(app::images::close, "Close tab")
        .OnClick([session, favicons, id] {
          session.Update([id](BrowserSession& current) {
            current.CloseTab(id);
          });
          favicons.Update([id](FaviconMap& values) {
            values.erase(id);
          });
        })
        .With(
            Frame{.width = kTabCloseButtonSize, .height = kTabCloseButtonSize},
            Enabled(close_visible),
            Opacity(AnimateTo(close_visible ? 1.0F : 0.0F, close_motion)),
            Tooltip("Close tab")
        ),
  }.With(
      Frame{.width = width, .height = kTabHeight},
      Padding(EdgeInsets::Symmetric(kTabHorizontalPadding, 0.0F)),
      Spacing(kTabSpacing),
      CrossAlign(CrossAxisAlignment::Center),
      CornerRadius(CornerRadii::Top(kTabCornerRadius)),
      Background(background),
      ClipChildren(),
      Foreground(foreground),
      Focusable(true),
      Semantics{.role = SemanticRole::Button, .label = tab.title},
      Opacity(AnimateTo(appeared.Get() ? 1.0F : 0.0F, entry_motion)),
      Offset(AnimateTo(
          appeared.Get() ? Point{} : Point{kTabEntryOffsetX, 0.0F}, entry_motion
      )),
      Tooltip(tab.url.empty() ? tab.title : tab.url)
  )
      .On<ViewEvents::Hover>([hovered](const HoverEvent& event) {
        hovered = event.type != HoverEventType::Leave;
      })
      .OnClick([session, id] {
        session.Update([id](BrowserSession& current) {
          current.SelectTab(id);
        });
      })
      .Key(id);
}

struct RevealBrowserTab {
  class Extension;
  ScrollController scroll;
  TabId active_id = 0;
  std::size_t active_index = 0;
  float tab_width = 0.0F;
  bool operator==(const RevealBrowserTab&) const = default;
};

class RevealBrowserTab::Extension final : public NodeExtension {
public:
  Extension(ViewNode& node, const RevealBrowserTab& spec) {
    Update(node, spec);
  }

  void Update(ViewNode&, const RevealBrowserTab& spec) {
    pending_ = pending_ || active_id_ != spec.active_id ||
               active_index_ != spec.active_index || tab_width_ != spec.tab_width ||
               !(scroll_ == spec.scroll);
    scroll_ = spec.scroll;
    active_id_ = spec.active_id;
    active_index_ = spec.active_index;
    tab_width_ = spec.tab_width;
  }

  FrameResult OnFrame(ViewNode&, const FrameInfo&) override {
    if (pending_ && CenterActive()) {
      pending_ = false;
    }
    return {.needs_frame = pending_};
  }

  PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer&) override {
    tab_offsets_.clear();
    if (!scroll_.IsConnected()) {
      return PaintInvalidation::None;
    }
    for (std::size_t index = 0; index < node.ChildCount(); ++index) {
      const auto& chip = node.ChildAt(index);
      tab_offsets_.push_back({chip.LayoutOffset().x, chip.LayoutSize().width});
    }
    if (active_index_ >= tab_offsets_.size()) {
      pending_ = false;
      return PaintInvalidation::None;
    }
    if (CenterActive()) {
      pending_ = false;
    }
    return PaintInvalidation::None;
  }

private:
  bool CenterActive() {
    if (!scroll_.IsConnected() || active_index_ >= tab_offsets_.size()) {
      return false;
    }
    const auto metrics = scroll_.Metrics();
    if (metrics.viewport_extent <= 0.0F) {
      return false;
    }
    const auto [x, width] = tab_offsets_[active_index_];
    const float target = std::clamp(
        x + width * 0.5F - metrics.viewport_extent * 0.5F,
        0.0F, metrics.maximum_offset
    );
    return target == metrics.offset || scroll_.ScrollTo(target);
  }

  ScrollController scroll_;
  TabId active_id_ = 0;
  std::size_t active_index_ = 0;
  float tab_width_ = 0.0F;
  std::vector<std::pair<float, float>> tab_offsets_;
  bool pending_ = true;
};

} // namespace

[[huxerui::composable]]
View BrowserTabStrip(State<BrowserSession> session, State<FaviconMap> favicons) {
  auto scroll = UseScrollController();
  auto available_width = UseState(0.0F);
  const BrowserSession& snapshot = session.Get();
  const std::size_t count = snapshot.Tabs().size();
  const float tab_width = available_width.Get() > 0.0F
                              ? std::clamp(
                                    (available_width.Get() - kNewTabWidth) /
                                        static_cast<float>(count),
                                    kMinimumTabWidth, kPreferredTabWidth
                                )
                              : kPreferredTabWidth;

  Views tabs;
  std::size_t active_index = 0;
  const FaviconMap& loaded_favicons = favicons.Get();
  for (std::size_t index = 0; index < count; ++index) {
    const TabInfo& tab = snapshot.Tabs()[index];
    if (tab.id == snapshot.ActiveTab()) {
      active_index = index;
    }
    const auto favicon = loaded_favicons.find(tab.id);
    std::optional<ImageAsset> icon;
    if (favicon != loaded_favicons.end()) {
      icon = favicon->second;
    }
    tabs.Add(BrowserTabChip(
        tab,
        tab.id == snapshot.ActiveTab(),
        session,
        favicons,
        std::move(icon),
        tab_width
    ));
  }
  tabs.Add(
      IconButton(app::images::add, "New tab")
          .OnClick([session] {
            session.Update([](BrowserSession& current) {
              current.NewTab();
            });
          })
          .With(Frame{.width = kNewTabButtonSize, .height = kNewTabButtonSize}, Tooltip("New tab"))
          .Key("new-tab")
  );

  const float content_width = static_cast<float>(count) * tab_width + kNewTabWidth;
  return Row {
    ScrollView(
        Row(std::move(tabs)).With(
            CrossAlign(CrossAxisAlignment::Center),
            RevealBrowserTab{scroll, snapshot.ActiveTab(), active_index, tab_width}
        )
    )
        .ScrollAxis(Axis::Horizontal)
        .Controller(scroll)
        .With(Frame{.height = kTabStripHeight, .max_width = content_width}),
    Spacer().With(WindowDragRegion{}),
  }.With(
      Grow(),
      CrossAlign(CrossAxisAlignment::Stretch),
      MeasureSize([available_width](Size size) {
        if (size.width > 0.0F && std::abs(size.width - available_width.Get()) > kGeometryTolerance) {
          available_width = size.width;
        }
      })
  );
}

} // namespace hoku
