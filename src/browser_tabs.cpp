#include "browser_tabs.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <app_resources.h>
#include <huxerui/huxerui.h>

#include "browser_ui.h"
#include "destination.h"

namespace hoku {
namespace {

using namespace huxerui;

constexpr float kPreferredTabWidth = 176.0F;
constexpr float kMinimumTabWidth = 32.0F;
constexpr float kTabStripButtonSize = 28.0F;
constexpr float kTabSearchWidth = 360.0F;
constexpr float kTabSearchListHeight = 296.0F;
constexpr float kTabStripHeight = 34.0F;
constexpr float kTabHeight = 30.0F;
constexpr float kTabIconSize = 16.0F;
constexpr float kTabTitleHeight = 20.0F;
constexpr float kTabTitleAllowance = 80.0F;
constexpr float kTabCloseButtonSize = 22.0F;
constexpr float kTabCloseIconSize = 16.0F;
constexpr float kTabHorizontalPadding = 8.0F;
constexpr float kTabSpacing = 4.0F;
constexpr float kSeparateTabControlsWidth =
    kTabIconSize + kTabCloseButtonSize + 2.0F * (kTabHorizontalPadding + kTabSpacing);
constexpr float kTabCornerRadius = 10.0F;
constexpr float kTabSeamRadius = 8.0F;
constexpr float kNewTabLeadingGap = kTabSeamRadius + 2.0F;
constexpr float kNewTabWidth = kNewTabLeadingGap + kTabStripButtonSize + 2.0F;
constexpr float kTabStripTrailingDragWidth = 20.0F;
constexpr float kTabSeparatorHeight = 16.0F;
constexpr float kTabMotionOffsetX = 10.0F;
constexpr float kTabHoverInset = 4.0F;
constexpr float kTabHoverAlpha = 0.06F;
constexpr float kGeometryTolerance = 0.5F;
constexpr double kTabEntryMotionSeconds = 0.18;
constexpr double kTabCloseButtonMotionSeconds = 0.10;

IconButtonStyle RoundTabButtonStyle(const ThemeSpec& theme, float size) {
  auto style = detail::DefaultIconButtonStyle(theme);
  style.minimum_interactive_size = size;
  style.state_layer_size = size;
  style.corner_radius = size * 0.5F;
  return style;
}

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

std::string TabSite(const TabInfo& tab) {
  const std::string origin = HttpOrigin(tab.url);
  if (origin.empty()) {
    return {};
  }
  const std::string_view authority = std::string_view(origin).substr(origin.find("://") + 3);
  const auto userinfo = authority.rfind('@');
  return std::string(userinfo == std::string_view::npos ? authority : authority.substr(userinfo + 1));
}

StringVariant TabDisplayTitle(const TabInfo& tab, const std::string& site) {
  if (tab.kind == TabKind::Settings) return app::strings::settings;
  if (tab.url.empty()) return app::strings::new_tab;
  if (tab.kind == TabKind::Web && tab.title == tab.url) {
    return site.empty() ? StringVariant(app::strings::web_page) : StringVariant(site);
  }
  return tab.title;
}

std::string TabHoverText(std::string title, const std::string& site) {
  // TODO: Add thumbnails and memory only when WebView exposes capture eligibility and attributable memory.
  if (!site.empty() && site != title) {
    title += '\n';
    title += site;
  }
  return title;
}

std::string FoldSearchText(std::string_view text) {
  std::string folded(text);
  std::ranges::transform(folded, folded.begin(), [](char character) {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : character;
  });
  return folded;
}

std::optional<ImageAsset> TabFavicon(const FaviconMap& favicons, TabId id) {
  const auto favicon = favicons.find(id);
  return favicon == favicons.end() ? std::nullopt : std::optional{favicon->second};
}

Image TabIcon(const TabInfo& tab, const std::optional<ImageAsset>& favicon, Color tint) {
  if (favicon) {
    return Image(*favicon);
  }
  return Image(tab.kind == TabKind::Settings ? app::images::settings : app::images::globe).Tint(tint);
}

[[huxerui::composable]]
View TabSearchResult(
    const TabInfo& tab, std::optional<ImageAsset> favicon, bool active, std::function<void(TabId)> select
) {
  const auto& colors = UseTheme().colors;
  auto hovered = UseState(false);
  const std::string site = TabSite(tab);
  const std::string title = UseString(TabDisplayTitle(tab, site));
  Views labels;
  labels.Add(Text(title, TextRole::Label).LineLimit(1).Overflow(TextOverflow::Ellipsis));
  if (!site.empty() && site != title) {
    labels.Add(
        Text(site, TextRole::Label)
            .LineLimit(1)
            .Overflow(TextOverflow::Ellipsis)
            .With(FontSize(12.0F), Foreground(colors.on_surface_variant))
    );
  }

  return Row {
    TabIcon(tab, favicon, active ? colors.primary : colors.on_surface_variant)
        .With(Frame{.width = kTabIconSize, .height = kTabIconSize}),
    Column(std::move(labels)).With(Grow(), CrossAlign(CrossAxisAlignment::Stretch)),
  }.With(
      Frame{.min_height = 44.0F},
      Padding(EdgeInsets::Symmetric(10.0F, 6.0F)),
      Spacing(10.0F),
      CrossAlign(CrossAxisAlignment::Center),
      CornerRadius(8.0F),
      Background(active || hovered.Get() ? colors.surface_container_highest : Color::Transparent()),
      Focusable(true),
      Semantics{.role = SemanticRole::Button, .label = title, .selected = active}
  )
      .On<ViewEvents::Hover>([hovered](const HoverEvent& event) {
        hovered = event.type != HoverEventType::Leave;
      })
      .OnClick([select = std::move(select), id = tab.id] { select(id); })
      .Key(tab.id);
}

[[huxerui::composable]]
View TabSearchPanel(PopupContext popup, State<BrowserSession> session, State<FaviconMap> favicons) {
  auto query = UseState(TextEditingValue{});
  auto search_field = UseViewHandle();
  auto tasks = UseTaskScope();
  Lifecycle([search_field] { search_field.RequestFocus(); }, 0);
  const auto& colors = UseTheme().colors;
  auto field_style = UseEnvironment<TextFieldStyle>();
  field_style.show_label = false;
  field_style.text_style.foreground = colors.on_surface;
  field_style.placeholder_style.foreground = colors.on_surface_variant;
  field_style.leading_icon = colors.on_surface_variant;
  field_style.focused_leading_icon = colors.primary;
  field_style.caret = colors.primary;
  field_style.outlined.border = colors.outline;
  field_style.outlined.hovered_border = colors.on_surface_variant;
  field_style.outlined.focused_border = colors.primary;
  const std::string filter = FoldSearchText(query->text);
  const auto& snapshot = session.Get();
  const auto& icons = favicons.Get();
  const auto select = [session, popup](TabId id) {
    if (popup.Dismiss()) {
      session.Update([id](BrowserSession& current) { current.SelectTab(id); });
    }
  };

  Views results;
  std::optional<TabId> first_match;
  for (const TabInfo& tab : snapshot.Tabs()) {
    if (FoldSearchText(UseString(TabDisplayTitle(tab, TabSite(tab)))).find(filter) == std::string::npos &&
        FoldSearchText(tab.url).find(filter) == std::string::npos) {
      continue;
    }
    if (!first_match) {
      first_match = tab.id;
    }
    results.Add(TabSearchResult(tab, TabFavicon(icons, tab.id), tab.id == snapshot.ActiveTab(), select));
  }
  if (!first_match) {
    results.Add(Text(app::strings::no_matching_tabs, TextRole::Body)
                    .With(Padding(10.0F), Foreground(colors.on_surface_variant)));
  }
  return Column {
    Theme(
        ThemeDefinition{}.Set(std::move(field_style)),
        TextField(query)
            .Label(app::strings::search_tabs)
            .Placeholder(app::strings::search_tabs)
            .LeadingIcon(app::images::search)
            .Variant(TextFieldVariant::Outlined)
            .InputConfiguration({.type = TextInputType::Text, .action = TextInputAction::Search})
            .OnChanged([query](const TextEditingValue& value) { query = value; })
            .OnSubmitted([first_match, select, tasks] {
              if (first_match) {
                // Dismissing the popup ends text input, so finish dispatching the key first.
                tasks.Post([id = *first_match, select] { select(id); });
              }
            })
            .Key("tab-search-field")
            .With(search_field)
    ),
    ScrollView(Column(std::move(results)).With(CrossAlign(CrossAxisAlignment::Stretch)))
        .With(Frame{.max_height = kTabSearchListHeight}, ScrollBar()),
  }.With(
      Frame{.width = kTabSearchWidth},
      Padding(10.0F),
      Spacing(8.0F),
      CrossAlign(CrossAxisAlignment::Stretch),
      Background(colors.surface_container),
      CornerRadius(12.0F),
      Shadow{Color::Rgb(0, 0, 0, 0.24F), {0.0F, 4.0F}, 12.0F, 0.0F},
      ClipChildren()
  ).Key("tab-search-panel");
}

[[huxerui::composable]]
View BrowserTabTitle(const TabInfo& tab, float max_width) {
  const ThemeSpec& theme = UseTheme();
  const std::string title = UseString(TabDisplayTitle(tab, TabSite(tab)));
  const auto style = detail::DefaultTextStyle(theme, TextRole::Label);
  const TextLayoutOptions no_wrap{.wrap = TextWrap::NoWrap};
  TextMeasurer& measurer = UseTextMeasurer();
  if (measurer.MeasureText(title, style, std::numeric_limits<float>::infinity(), no_wrap)
          .size.width <= max_width) {
    return Text(title, TextRole::Label);
  }

  constexpr std::size_t kSourceLimit = 60;
  std::size_t low = 0;
  std::size_t high = kSourceLimit;
  while (low < high) {
    const std::size_t middle = (low + high + 1) / 2;
    if (measurer.MeasureText(Utf8Prefix(title, middle), style,
                             std::numeric_limits<float>::infinity(), no_wrap)
            .size.width <= max_width) {
      low = middle;
    } else {
      high = middle - 1;
    }
  }
  return Text(std::string(Utf8Prefix(title, low)), TextRole::Label);
}

struct TabChipDecoration {
  std::optional<Color> hover_background;
  std::optional<bool> after;
  Color insertion_color;
  bool operator==(const TabChipDecoration&) const = default;

  class Extension;
};

class TabChipDecoration::Extension final : public NodeExtension {
public:
  Extension(ViewNode&, const TabChipDecoration& decoration) : decoration_(decoration) {}

  void Update(ViewNode&, const TabChipDecoration& decoration) {
    if (decoration_ != decoration) {
      decoration_ = decoration;
      InvalidatePaint();
    }
  }

  void PaintBehindContent(const ViewNode& node, PaintContext& paint) const override {
    const Size size = node.LayoutSize();
    if (decoration_.hover_background && size.width > 2.0F * kTabHoverInset) {
      paint.DrawRect({kTabHoverInset, 0.0F, size.width - 2.0F * kTabHoverInset, size.height},
                     *decoration_.hover_background, CornerRadii{kTabCornerRadius});
    }
  }

  void PaintAboveContent(const ViewNode& node, PaintContext& paint) const override {
    if (decoration_.after) {
      const Size size = node.LayoutSize();
      paint.DrawRect({*decoration_.after ? size.width - 3.0F : 1.0F, 4.0F, 2.0F, size.height - 8.0F},
                     decoration_.insertion_color);
    }
  }

private:
  TabChipDecoration decoration_;
};

struct TrackTabBounds {
  std::function<void(Rect)> publish;

  class Extension final : public NodeExtension {
  public:
    Extension(ViewNode&, const TrackTabBounds& tracking) : publish_(tracking.publish) {}

    void Update(ViewNode&, const TrackTabBounds& tracking) {
      publish_ = tracking.publish;
    }

    PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer&) override {
      publish_(node.PresentationBounds());
      return PaintInvalidation::None;
    }

  private:
    std::function<void(Rect)> publish_;
  };
};

struct TabRowSlot {
  struct Value {
    TabId id;
    float left;
    bool operator==(const Value&) const = default;
  };
};

struct TabCloseLayout {
  float tab_width;
  float strip_width;
  bool operator==(const TabCloseLayout&) const = default;
};

struct TabChipHovered { using Value = bool; };
struct TabScrollViewport { using Value = bool; };

const ViewNode* FindTabScrollViewport(const ViewNode& node) {
  if (node.LayoutValue<TabScrollViewport>()) {
    return &node;
  }
  for (std::size_t index = 0; index < node.ChildCount(); ++index) {
    if (const auto* viewport = FindTabScrollViewport(node.ChildAt(index))) {
      return viewport;
    }
  }
  return nullptr;
}

struct TabSeamGeometry {
  Rect tab;
  Rect clip;
  float opacity;
  bool operator==(const TabSeamGeometry&) const = default;
};

std::optional<TabSeamGeometry> MeasureTabSeam(const ViewNode& title_bar, TabId active_tab) {
  const auto* viewport = FindTabScrollViewport(title_bar);
  if (!viewport || viewport->ChildCount() != 1) {
    return std::nullopt;
  }
  const auto& row = viewport->ChildAt(0);
  const Rect visible = viewport->PresentationBounds();
  const Point origin = title_bar.LocalToWindow({});
  for (std::size_t index = 0; index < row.ChildCount(); ++index) {
    const auto& scope = row.ChildAt(index);
    const auto* slot = scope.LayoutValue<TabRowSlot>();
    if (!slot || slot->id != active_tab || scope.ChildCount() != 1) {
      continue;
    }
    const auto& chip = scope.ChildAt(0);
    const Rect tab = chip.PresentationBounds();
    const float opacity = chip.PresentationOpacity();
    if (!tab.Intersects(visible) || opacity <= 0.0F) {
      return std::nullopt;
    }
    float clip_left = visible.x;
    // At the start of the strip, use the gap to the tab-search button for the left curve.
    if (title_bar.ChildCount() > 1 && row.PresentationBounds().x >= visible.x) {
      const Rect search = title_bar.ChildAt(0).PresentationBounds();
      clip_left = std::clamp(search.x + search.width, visible.x - kTabSeamRadius, visible.x);
    }
    const float seam_y = title_bar.Bounds().height;
    return TabSeamGeometry{
        .tab = {tab.x - origin.x, tab.y - origin.y, tab.width, seam_y - (tab.y - origin.y)},
        .clip = {clip_left - origin.x, 0.0F, visible.x + visible.width - clip_left, seam_y + 1.0F},
        .opacity = opacity,
    };
  }
  return std::nullopt;
}

Path TabSeamPath(Rect tab) {
  constexpr float quarter_circle = 0.55228475F;
  const float left = tab.x;
  const float right = tab.x + tab.width;
  const float top = tab.y;
  const float bottom = tab.y + tab.height;
  const float corner = std::min({kTabCornerRadius, tab.width * 0.5F, tab.height * 0.5F});
  const float seam = std::min(kTabSeamRadius, tab.height - corner);
  const float corner_handle = corner * quarter_circle;
  const float seam_handle = seam * quarter_circle;
  Path path;
  path.MoveTo({left - seam, bottom})
      .CubicTo({left - seam + seam_handle, bottom}, {left, bottom - seam + seam_handle}, {left, bottom - seam})
      .LineTo({left, top + corner})
      .CubicTo({left, top + corner - corner_handle}, {left + corner - corner_handle, top}, {left + corner, top})
      .LineTo({right - corner, top})
      .CubicTo({right - corner + corner_handle, top}, {right, top + corner - corner_handle}, {right, top + corner})
      .LineTo({right, bottom - seam})
      .CubicTo({right, bottom - seam + seam_handle}, {right + seam - seam_handle, bottom}, {right + seam, bottom})
      // Overlap the toolbar by one logical pixel so fractional scaling cannot open a seam.
      .LineTo({right + seam, bottom + 1.0F})
      .LineTo({left - seam, bottom + 1.0F})
      .Close();
  return path;
}

struct TabSeam {
  class Extension;
  std::shared_ptr<BrowserWindowState> model;
  TabId active_tab;
  Color color;
  bool operator==(const TabSeam&) const = default;
};

class TabSeam::Extension final : public NodeExtension {
public:
  Extension(ViewNode&, TabSeam seam) : seam_(seam) {}

  void Update(ViewNode&, TabSeam seam) {
    if (seam_ != seam) {
      seam_ = seam;
      InvalidatePaint(PaintInvalidation::Content);
    }
  }

  PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer&) override {
    seam_.model->title_bar_width = node.Bounds().width;
    auto geometry = MeasureTabSeam(node, seam_.active_tab);
    if (geometry_ == geometry) {
      return PaintInvalidation::None;
    }
    geometry_ = geometry;
    return PaintInvalidation::Content;
  }

  void PaintBehindContent(const ViewNode&, PaintContext& context) const override {
    if (!geometry_ || geometry_->tab.IsEmpty() || geometry_->clip.IsEmpty()) {
      return;
    }
    Color color = seam_.color;
    color.alpha *= geometry_->opacity;
    context.PushClip(geometry_->clip);
    context.FillPath(TabSeamPath(geometry_->tab), color);
    context.PopClip();
  }

private:
  TabSeam seam_;
  std::optional<TabSeamGeometry> geometry_;
};

class TabRow final : public Layout<TabRow> {
public:
  using Layout::Layout;

  static LayoutResult Measure(LayoutContext& context, ViewNode& node, Constraints constraints) {
    LayoutResult result;
    float right = 0.0F;
    const float height = constraints.Constrain({0.0F, kTabHeight}).height;
    for (ViewNode& child : node.Children()) {
      const Size size = context.Measure(child, constraints.Loose());
      const auto slot = *child.LayoutValue<TabRowSlot>();
      // Tab offsets animate in row coordinates. The slots still determine the scrollable extent.
      result.Place(child, {slot.id == 0 ? slot.left : 0.0F, (height - size.height) * 0.5F});
      right = std::max(right, slot.left + size.width);
    }
    return result.SetSize(constraints.Constrain({right, height}));
  }
};

struct TrackTabRow {
  std::shared_ptr<BrowserWindowState> model;
  TabId active_tab;
  Color separator_color;
  bool operator==(const TrackTabRow&) const = default;

  class Extension final : public NodeExtension {
  public:
    Extension(ViewNode&, const TrackTabRow& tracking)
        : model_(tracking.model), active_tab_(tracking.active_tab), separator_color_(tracking.separator_color) {}

    void Update(ViewNode&, const TrackTabRow& tracking) {
      model_ = tracking.model;
      active_tab_ = tracking.active_tab;
      if (separator_color_ != tracking.separator_color) {
        separator_color_ = tracking.separator_color;
        InvalidatePaint(PaintInvalidation::Content);
      }
    }

    PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer&) override {
      const Point origin = node.LocalToWindow({});
      model_->tab_row_left = origin.x;
      std::vector<Point> separators;
      std::optional<Rect> previous_tab;
      float previous_slot_end = 0.0F;
      for (const ViewNode& child : node.Children()) {
        const auto slot = *child.LayoutValue<TabRowSlot>();
        if (slot.id == 0 || child.ChildCount() != 1) {
          continue;
        }
        const auto& chip = child.ChildAt(0);
        const Rect bounds = chip.PresentationBounds();
        model_->tab_bounds.insert_or_assign(slot.id, bounds);
        if (bounds.IsEmpty() || chip.PresentationOpacity() <= 0.0F) {
          continue;
        }
        if (slot.id == active_tab_ || chip.LayoutValueOr<TabChipHovered>(false)) {
          previous_tab.reset();
          continue;
        }
        // A moving tab can overlap its neighbors. Draw only where adjacent slots meet on screen.
        if (previous_tab && std::abs(previous_slot_end - slot.left) <= kGeometryTolerance &&
            std::abs(previous_tab->x + previous_tab->width - bounds.x) <= kGeometryTolerance) {
          separators.emplace_back(bounds.x - origin.x, bounds.y + bounds.height * 0.5F - origin.y);
        }
        previous_tab = bounds;
        previous_slot_end = slot.left + child.LayoutSize().width;
      }
      if (separators_ == separators) {
        return PaintInvalidation::None;
      }
      separators_ = std::move(separators);
      return PaintInvalidation::Content;
    }

    void PaintBehindContent(const ViewNode&, PaintContext& paint) const override {
      for (const Point center : separators_) {
        paint.DrawLine({center.x, center.y - kTabSeparatorHeight * 0.5F},
                       {center.x, center.y + kTabSeparatorHeight * 0.5F}, separator_color_, StrokeStyle{.width = 1.0F});
      }
    }

  private:
    std::shared_ptr<BrowserWindowState> model_;
    TabId active_tab_;
    Color separator_color_;
    std::vector<Point> separators_;
  };
};

[[huxerui::composable]]
View BrowserTabChip(
    const TabInfo& tab,
    bool active,
    std::shared_ptr<BrowserWindowState> model,
    const std::function<void(TabId)>& detach_tab,
    bool live_drag,
    bool ghost,
    std::optional<ImageAsset> favicon,
    const std::function<void(TabId, bool)>& close_tab,
    bool snap_position,
    float width,
    float slot_left,
    std::optional<float> drag_left
) {
  const ThemeSpec& theme = UseTheme();
  const auto window = UseWindow();
  const auto tasks = UseApplicationTaskScope();
  const auto menu = UseMenu();
  const auto session = model->session;
  auto hovered = UseState(false);
  auto appeared = UseState(theme.motion.reduced_motion);
  auto insertion = UseState(std::optional<bool>{});
  Lifecycle([appeared] {
    appeared = true;
  }, 0);

  const TweenSpec entry_motion{
      .duration = theme.motion.reduced_motion ? 0.0 : kTabEntryMotionSeconds,
      .easing = Easing::EaseOut,
  };
  const TweenSpec close_motion{
      .duration = theme.motion.reduced_motion ? 0.0 : kTabCloseButtonMotionSeconds,
      .easing = Easing::EaseOut,
  };
  const bool compact = width < kSeparateTabControlsWidth;
  const bool is_hovered = hovered.Get();
  const bool close_visible = active || (!compact && is_hovered);
  Color hover_background = theme.colors.on_surface;
  hover_background.alpha = kTabHoverAlpha;
  const Color background = active && drag_left ? theme.colors.surface_container_highest : Color::Transparent();
  auto indication = theme.interactions.indication;
  indication.hover.reset();
  if (active) {
    indication.press.reset();
    indication.ripple.reset();
  }
  const Color foreground = active ? theme.colors.on_surface : theme.colors.on_surface_variant;
  const float title_width = std::max(0.0F, width - kTabTitleAllowance);
  const TabId id = tab.id;
  auto close_style = RoundTabButtonStyle(theme, kTabCloseButtonSize);
  close_style.icon_size = kTabCloseIconSize;
  const Point slot_position{slot_left + (appeared.Get() ? 0.0F : kTabMotionOffsetX), 0.0F};
  const bool follow_pointer = snap_position && appeared.Get();
  const auto offset = drag_left      ? Offset(Point{*drag_left, 0.0F})
                    : follow_pointer ? Offset(slot_position)
                                     : Offset(AnimateTo(slot_position, entry_motion));

  View icon = TabIcon(tab, favicon, active ? theme.colors.primary : theme.colors.on_surface_variant)
      .With(Frame{.width = kTabIconSize, .height = kTabIconSize});
  View close_button = Theme(
      ThemeDefinition{}.Set(std::move(close_style)),
      IconButton(app::images::close, app::strings::close_tab)
          .OnClick([close_tab, hovered, id] { close_tab(id, hovered.Get()); })
          .Key("close-tab")
          .With(
              Frame{.width = kTabCloseButtonSize, .height = kTabCloseButtonSize},
              Enabled(close_visible),
              Opacity(AnimateTo(close_visible ? 1.0F : 0.0F, close_motion)),
              Tooltip(app::strings::close_tab)
          )
  );
  Views contents;
  if (compact) {
    // Center the favicon and the active close button in the same slot at the narrowest widths.
    contents.Add(Stack {
      active ? std::move(close_button) : std::move(icon),
    }.With(Grow(), Align(HorizontalAlignment::Center, VerticalAlignment::Center)));
  } else {
    contents.Add(std::move(icon));
    contents.Add(Row {
      title_width > 0.0F ? BrowserTabTitle(tab, title_width) : View{},
    }.With(Grow(), Frame{.height = kTabTitleHeight}, ClipChildren()));
    contents.Add(std::move(close_button));
  }
  View chip = Row(std::move(contents)).With(
      Frame{.width = width, .height = kTabHeight},
      Padding(EdgeInsets::Symmetric(compact ? 0.0F : kTabHorizontalPadding, 0.0F)),
      Spacing(kTabSpacing),
      CrossAlign(CrossAxisAlignment::Center),
      CornerRadius(active ? CornerRadii::Top(kTabCornerRadius) : CornerRadii{kTabCornerRadius}),
      Background(background),
      indication,
      ClipChildren(),
      Foreground(foreground),
      TabChipDecoration{!active && is_hovered ? std::optional{hover_background} : std::nullopt,
                        insertion.Get(), theme.colors.primary},
      Focusable(true),
      Semantics{.role = SemanticRole::Button, .label = TabDisplayTitle(tab, TabSite(tab)), .hidden = ghost},
      ghost ? Opacity(0.0F) : Opacity(AnimateTo(appeared.Get() ? 1.0F : 0.0F, entry_motion)),
      offset
  )
      .On<ViewEvents::Hover>([hovered](const HoverEvent& event) {
        hovered = event.type != HoverEventType::Leave;
      })
      .On<ViewEvents::PointerIntercept>([close_tab, id, ghost](const PointerEvent& event) {
        if (!ghost && event.type == PointerEventType::Down && event.changed_button == PointerButton::Middle) {
          close_tab(id, false);
          return true;
        }
        return false;
      })
      .OnClick([session, id] {
        session.Update([id](BrowserSession& current) {
          current.SelectTab(id);
        });
      });

  const auto update_insertion = [insertion, width, model, id](const BrowserTabDrag& source, const DropEvent& event) {
    if (source.source != model || source.id != id) {
      insertion = event.position.x >= width * 0.5F;
    }
  };
  if (!live_drag) chip = std::move(chip)
      .With(
          DragSource(BrowserTabDrag{model, window, id}, [tab, favicon, colors = theme.colors, width] {
            return Row {
              TabIcon(tab, favicon, colors.primary).With(Frame{.width = kTabIconSize, .height = kTabIconSize}),
              Text(TabDisplayTitle(tab, TabSite(tab)), TextRole::Label),
            }.With(Frame{.width = width, .height = kTabHeight}, Padding(EdgeInsets::Symmetric(8.0F, 0.0F)),
                   Spacing(6.0F), CrossAlign(CrossAxisAlignment::Center), Background(colors.surface_container_highest),
                   Foreground(colors.on_surface), CornerRadius(8.0F), ClipChildren());
          }).Scope(DragDropScope::Application),
          DropTarget::Accepts<BrowserTabDrag>().Scope(DragDropScope::Application)
      )
      .On<DragSourceEvents::Ended>([detach_tab, session, id](const auto& result) {
        if constexpr (requires { result.released_outside_application; }) {
          if (result.released_outside_application && detach_tab && session->Tabs().size() > 1) {
            detach_tab(id);
          }
        }
      })
      .On<DropEvents<BrowserTabDrag>::Entered>(update_insertion)
      .On<DropEvents<BrowserTabDrag>::Moved>(update_insertion)
      .On<DropEvents<BrowserTabDrag>::Exited>([insertion](const BrowserTabDrag&, const DropEvent&) {
        insertion = std::nullopt;
      })
      .On<DropEvents<BrowserTabDrag>::Dropped>(
          [insertion, model, window, tasks, id, width](const BrowserTabDrag& source, const DropEvent& event) {
            insertion = std::nullopt;
            tasks.Post([source, model, window, id, after = event.position.x >= width * 0.5F] {
              DropBrowserTab(source, model, window, id, after);
            });
          });
  if (detach_tab) {
    chip = std::move(chip).On<ViewEvents::ContextMenuRequested>([menu, detach_tab, id](Point point) {
      menu.ShowAt(point, {MenuItem(app::strings::move_tab, [detach_tab, id] { detach_tab(id); })});
    });
  }
  if (!active && !ghost) {
    const std::string site = TabSite(tab);
    chip = std::move(chip).With(Tooltip(TabHoverText(UseString(TabDisplayTitle(tab, site)), site)));
  }
  return std::move(chip).Key("tab-chip").LayoutValue<TabChipHovered>(is_hovered);
}

struct RevealBrowserTab {
  class Extension;
  ScrollController scroll;
  TabId active_id = 0;
  std::size_t active_index = 0;
  float tab_width = 0.0F;
  bool defer_reveal = false;
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
               !(scroll_ == spec.scroll) || defer_reveal_ != spec.defer_reveal;
    scroll_ = spec.scroll;
    active_id_ = spec.active_id;
    active_index_ = spec.active_index;
    tab_width_ = spec.tab_width;
    defer_reveal_ = spec.defer_reveal;
  }

  FrameResult OnFrame(ViewNode&, const FrameInfo&) override {
    if (pending_ && CenterActive()) {
      pending_ = false;
    }
    return {.needs_frame = pending_ && !defer_reveal_};
  }

  PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer&) override {
    tab_offsets_.clear();
    if (!scroll_.IsConnected()) {
      return PaintInvalidation::None;
    }
    for (std::size_t index = 0; index < node.ChildCount(); ++index) {
      const auto& chip = node.ChildAt(index);
      tab_offsets_.push_back({chip.LayoutValue<TabRowSlot>()->left, chip.LayoutSize().width});
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
    if (defer_reveal_ || !scroll_.IsConnected() || active_index_ >= tab_offsets_.size()) {
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
  bool defer_reveal_ = false;
};

} // namespace

View WithBrowserTabSeam(View title_bar, std::shared_ptr<BrowserWindowState> model, Color color) {
  const TabId active_tab = model->session->ActiveTab();
  return std::move(title_bar).With(TabSeam{std::move(model), active_tab, color});
}

void SlideBrowserTab(BrowserWindowState& model, TabId id, float left) {
  const auto bounds = model.tab_bounds.find(id);
  const auto& tabs = model.session->Tabs();
  const auto dragged = std::ranges::find(tabs, id, &TabInfo::id);
  if (bounds == model.tab_bounds.end() || bounds->second.width <= 0.0F || dragged == tabs.end()) {
    return;
  }
  const float width = bounds->second.width;
  const auto index = static_cast<std::size_t>(std::distance(tabs.begin(), dragged));
  const auto last = tabs.size() - 1;
  const float translated_left = std::clamp(left - model.tab_row_left, 0.0F, width * static_cast<float>(last));
  model.tab_slide = TabSlide{id, translated_left, std::nullopt};

  constexpr float reorder_hysteresis = 4.0F;
  const float distance = translated_left - width * static_cast<float>(index);
  const float nearest = std::round((translated_left - std::copysign(reorder_hysteresis, distance)) / width);
  const auto target = static_cast<std::size_t>(std::clamp(nearest, 0.0F, static_cast<float>(last)));
  auto next = model.session.Get();
  if (target == index) {
    if (next.SelectTab(id)) {
      model.session = std::move(next);
    }
    return;
  }
  const auto insertion = target < index ? target : target + 1;
  const std::optional<TabId> before = insertion < tabs.size() ? std::optional{tabs[insertion].id} : std::nullopt;
  if (next.ReorderTab(id, before)) {
    model.session = std::move(next);
  }
}

[[huxerui::composable]]
View BrowserTabSearchButton(State<BrowserSession> session, State<FaviconMap> favicons) {
  auto popup = UsePopup();
  auto anchor = UseViewHandle();
  return Theme(
      ThemeDefinition{}.Set(RoundTabButtonStyle(UseTheme(), kTabStripButtonSize)),
      IconButton(app::images::chevron_down, app::strings::search_tabs)
          .OnClick([popup, anchor, session, favicons] {
            popup.Show(
                anchor,
                [session, favicons](PopupContext context) { return TabSearchPanel(context, session, favicons); },
                PopupOptions{
                    .placement = {AnchorSide::Below, AnchorAlignment::Start},
                    .trap_focus = true,
                }
            );
          })
          .With(anchor, Frame{.width = kTabStripButtonSize, .height = kTabStripButtonSize}, Tooltip(app::strings::search_tabs))
          .Key("tab-search-button")
  );
}

[[huxerui::composable]]
View BrowserTabStrip(
    std::shared_ptr<BrowserWindowState> model,
    std::function<void(TabId)> detach_tab,
    TabDragHandlers drag
) {
  const auto session = model->session;
  const auto favicons = model->favicons;
  const auto window = UseWindow();
  const auto tasks = UseApplicationTaskScope();
  auto scroll = UseScrollController();
  auto available_width = UseState(0.0F);
  auto close_layout = UseState(std::optional<TabCloseLayout>{});
  auto mouse_over_strip = UseState(false);
  const bool keep_close_position = model->keep_tab_close_position.Get();
  Lifecycle([close_layout, keep_close_position] {
    if (!keep_close_position) {
      close_layout = std::nullopt;
    }
  }, keep_close_position);
  const BrowserSession& snapshot = session.Get();
  const bool live_drag = static_cast<bool>(drag.started);
  const auto slide = model->tab_slide.Get();
  const auto detaching = slide && slide->detaching ? std::optional{slide->id} : std::nullopt;
  Lifecycle([detaching, source_ready = drag.source_ready] {
    if (detaching && source_ready) {
      source_ready();
    }
  }, detaching);
  const std::size_t tab_count = snapshot.Tabs().size();
  const std::size_t visible_count = tab_count - (detaching && snapshot.FindTab(*detaching) ? 1 : 0);
  const auto origin = model->drag_origin.Get();
  const bool has_ghost = origin && !snapshot.FindTab(origin->tab.id);
  const std::size_t ghost_index = has_ghost ? std::min(origin->index, tab_count) : tab_count;
  const std::size_t count = tab_count + (has_ghost ? 1 : 0);
  const auto frozen = keep_close_position ? close_layout.Get() : std::nullopt;
  const float tabs_width = std::max(0.0F, available_width.Get() - kTabStripTrailingDragWidth);
  const float fitted_width = available_width.Get() > 0.0F
                                 ? std::clamp((tabs_width - kNewTabWidth) / static_cast<float>(visible_count),
                                              kMinimumTabWidth, kPreferredTabWidth)
                                 : kPreferredTabWidth;
  const float tab_width = frozen ? frozen->tab_width : fitted_width;
  const auto close_tab = [tasks, model, window, close_layout, mouse_over_strip, tab_width](TabId id, bool hovered) {
    tasks.Post([tab = BrowserTabDrag{model, window, id}, close_layout, mouse_over_strip, tab_width, hovered] {
      const auto& tabs = tab.source->session->Tabs();
      const auto closing = std::ranges::find(tabs, tab.id, &TabInfo::id);
      if (closing == tabs.end()) {
        return;
      }
      if (hovered && mouse_over_strip.Get() && tab.source->keep_tab_close_position.Get() &&
          std::next(closing) != tabs.end()) {
        if (!close_layout.Get()) {
          close_layout = TabCloseLayout{tab_width, static_cast<float>(tabs.size()) * tab_width + kNewTabWidth};
        }
      } else {
        close_layout = std::nullopt;
      }
      CloseBrowserTab(tab);
    });
  };

  Views tabs;
  std::optional<View> dragged_chip;
  std::size_t active_index = 0;
  std::size_t tab_index = 0;
  const bool snap_positions = has_ghost || detaching || (frozen && !slide);
  float next_tab_left = slide && slide->detached_width ? slide->left : 0.0F;
  const FaviconMap& loaded_favicons = favicons.Get();
  for (std::size_t index = 0; index < count; ++index) {
    // The original hit route has to remain mounted until the pointer is released.
    const bool transferred = has_ghost && index == ghost_index;
    const TabInfo& tab = transferred ? origin->tab : snapshot.Tabs()[tab_index++];
    const bool ghost = transferred || detaching == tab.id;
    if (!ghost && tab.id == snapshot.ActiveTab()) {
      active_index = index;
    }
    const bool sliding = !ghost && slide && slide->id == tab.id;
    const auto detached_width = sliding ? slide->detached_width : std::nullopt;
    const float slot = next_tab_left;
    const float width = ghost ? 0.0F : detached_width.value_or(tab_width);
    next_tab_left += width;
    View chip = BrowserTabChip(
        tab,
        transferred ? origin->active : tab.id == snapshot.ActiveTab(),
        model,
        detach_tab,
        live_drag,
        ghost,
        TabFavicon(loaded_favicons, tab.id),
        close_tab,
        snap_positions,
        width,
        slot,
        sliding ? std::optional{slide->left} : std::nullopt
    ).Key(tab.id).LayoutValue<TabRowSlot>({tab.id, slot});
    if (sliding) {
      dragged_chip = std::move(chip);
    } else {
      tabs.Add(std::move(chip));
    }
  }
  const float new_tab_left = next_tab_left + kNewTabLeadingGap;
  tabs.Add(Row {
    Theme(
        ThemeDefinition{}.Set(RoundTabButtonStyle(UseTheme(), kTabStripButtonSize)),
        IconButton(app::images::add, app::strings::new_tab)
            .OnClick([session] {
              session.Update([](BrowserSession& current) {
                current.NewTab();
              });
            })
            .With(Frame{.width = kTabStripButtonSize, .height = kTabStripButtonSize}, Tooltip(app::strings::new_tab))
    ),
  }.Key("new-tab").LayoutValue<TabRowSlot>({0, new_tab_left}));
  if (dragged_chip) {
    // Paint the dragged chip above the neighbors while keeping its logical slot.
    tabs.Add(std::move(*dragged_chip));
  }

  const float content_width = std::max(next_tab_left + kNewTabWidth, frozen ? frozen->strip_width : 0.0F);
  const float viewport_width = available_width.Get() > 0.0F ? std::min(tabs_width, content_width) : content_width;
  Color separator_color = UseTheme().colors.outline;
  separator_color.alpha *= 0.55F;
  View tab_row = TabRow(std::move(tabs)).With(
      // Retain the scroll extent so closing near the right edge cannot shift the tabs back under the pointer.
      Frame{.min_width = frozen ? std::optional{frozen->strip_width} : std::nullopt},
      TrackTabRow{model, snapshot.ActiveTab(), separator_color},
      RevealBrowserTab{scroll, snapshot.ActiveTab(), active_index, tab_width, slide.has_value() || frozen.has_value()}
  );
  if (live_drag) {
    // The row keeps the gesture when its dragged tab is attached to another window.
    tab_row = std::move(tab_row).With(DragGesture{})
        .On<DragEvents::Started>(std::move(drag.started))
        .On<DragEvents::Changed>(std::move(drag.changed))
        .On<DragEvents::Ended>(std::move(drag.ended))
        .On<DragEvents::Canceled>([cancel = std::move(drag.canceled)](const DragEvent&) { cancel(); });
  }
  return Row {
    ScrollView(std::move(tab_row).Key("tab-row"))
        .ScrollAxis(Axis::Horizontal)
        .Controller(scroll)
        .With(Frame{.height = kTabStripHeight, .max_width = viewport_width})
        .LayoutValue<TabScrollViewport>(true),
    Spacer().With(WindowDragRegion{}),
  }.With(
      Grow(),
      CrossAlign(CrossAxisAlignment::Stretch),
      TrackTabBounds{[model](Rect bounds) { model->tab_strip_bounds = bounds; }},
      DropTarget::Accepts<BrowserTabDrag>().Scope(DragDropScope::Application),
      MeasureSize([available_width](Size size) {
        if (size.width > 0.0F && std::abs(size.width - available_width.Get()) > kGeometryTolerance) {
          available_width = size.width;
        }
      })
  ).On<ViewEvents::Hover>([mouse_over_strip, close_layout](const HoverEvent& event) {
    const bool over_strip = event.device_kind == PointerDeviceKind::Mouse && event.type != HoverEventType::Leave;
    mouse_over_strip = over_strip;
    if (!over_strip) {
      close_layout = std::nullopt;
    }
  }).On<DropEvents<BrowserTabDrag>::Dropped>([model, window, tasks](const BrowserTabDrag& source, const DropEvent&) {
    tasks.Post([model, window, source] { DropBrowserTab(source, model, window, std::nullopt, false); });
  });
}

} // namespace hoku
