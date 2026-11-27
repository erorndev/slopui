#pragma once

#include <yoga/Yoga.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "platform/Window.h"
#include "ui/Color.h"
#include "ui/Font.h"
#include "ui/RichText.h"

namespace lale::ui {

struct Rect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;

  bool Contains(float point_x, float point_y) const;
  Rect Intersect(const Rect& other) const;
};

enum class IconKind {
  kNone,
  kMinus,
  kPlus,
  kClose,
  kArrowUp,
  kArrowLeft,
  kStop,
  kChevronDown,
  kTulip,
  kWindowMinimize,
  kWindowMaximize,
  kWindowRestore,
  kWindowClose
};

struct Shadow {
  float offset_y = 0;
  float blur = 0;
  float spread = 0;
  Color color;
};

struct NodeStyle {
  std::vector<Shadow> shadows;
  Color background;
  float corner_radius = 0;
  Color border;
  float border_width = 0;
  Color outer_ring;
  float outer_ring_width = 0;

  std::vector<TextSpan> spans;
  std::string text;
  std::string placeholder;
  const Font* font = nullptr;
  Color text_color;
  Color placeholder_color;
  bool wraps_text = false;
  bool elides_text = false;
  bool shows_caret = false;
  size_t caret_byte = std::string::npos;
  float backdrop_blur = 0;
  size_t selection_begin = 0;
  size_t selection_end = 0;
  Color selection_color;

  IconKind icon = IconKind::kNone;
  Color icon_color;

  CursorKind cursor = CursorKind::kArrow;
  bool scrolls_vertically = false;
  float scroll_offset = 0;
};

inline constexpr float kCaretReservedWidth = 2.0f;

float TextWrapWidth(const NodeStyle& style, float box_width);
YGConfigRef SharedLayoutConfig();
void SetLayoutPointScale(float device_pixels_per_point);

class Node {
 public:
  explicit Node(std::string id = {});
  ~Node();
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  Node* AddChild(std::string id = {});
  void EnableTextMeasurement();

  YGNodeRef yoga_node() const { return yoga_node_; }
  const std::string& id() const { return id_; }
  void set_id(std::string id) { id_ = std::move(id); }
  NodeStyle& style() { return style_; }
  const NodeStyle& style() const { return style_; }
  const std::vector<std::unique_ptr<Node>>& children() const {
    return children_;
  }
  const Rect& bounds() const { return bounds_; }
  const Rect& visible_clip() const { return visible_clip_; }

  void ResolveBounds(float parent_x, float parent_y, const Rect& parent_clip);
  const Node* HitTest(float x, float y) const;
  Node* FindById(std::string_view id);

 private:
  YGNodeRef yoga_node_;
  std::string id_;
  NodeStyle style_;
  std::vector<std::unique_ptr<Node>> children_;
  Rect bounds_;
  Rect visible_clip_;
};

}
