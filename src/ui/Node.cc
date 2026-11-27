#include "ui/Node.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lale::ui {

namespace {

YGSize MeasureTextNode(YGNodeConstRef yoga_node, float available_width,
                       YGMeasureMode width_mode, float, YGMeasureMode) {
  const auto* node = static_cast<const Node*>(YGNodeGetContext(yoga_node));
  const NodeStyle& style = node->style();
  if (!style.spans.empty()) {
    const float wrap_width = width_mode == YGMeasureModeUndefined
                                 ? std::numeric_limits<float>::max()
                                 : available_width;
    float widest_line = 0;
    float total_height = 0;
    for (const RichTextLine& line : LayoutRichText(style.spans, wrap_width)) {
      widest_line = std::max(widest_line, line.width);
      total_height += line.height;
    }
    float measured_width = std::ceil(widest_line);
    if (width_mode == YGMeasureModeExactly) measured_width = available_width;
    if (width_mode == YGMeasureModeAtMost) {
      measured_width = std::min(measured_width, available_width);
    }
    return {measured_width, std::ceil(total_height)};
  }
  if (!style.font) return {0, 0};
  const std::string& shown_text =
      style.text.empty() ? style.placeholder : style.text;
  const float caret_width = style.shows_caret ? kCaretReservedWidth : 0.0f;
  const float line_height = style.font->line_height();

  if (style.wraps_text && width_mode != YGMeasureModeUndefined) {
    auto lines =
        style.font->Wrap(shown_text, TextWrapWidth(style, available_width));
    float widest_line = 0;
    for (const auto& line : lines) {
      widest_line = std::max(widest_line, line.width);
    }
    float measured_width =
        width_mode == YGMeasureModeExactly
            ? available_width
            : std::min(available_width, std::ceil(widest_line + caret_width));
    return {measured_width, line_height * static_cast<float>(lines.size())};
  }
  float single_line_width =
      std::ceil(style.font->Measure(shown_text) + caret_width);
  if (width_mode == YGMeasureModeAtMost) {
    single_line_width = std::min(single_line_width, available_width);
  }
  return {single_line_width, line_height};
}

}

YGConfigRef SharedLayoutConfig() {
  static YGConfigRef shared_layout_config = YGConfigNew();
  return shared_layout_config;
}

void SetLayoutPointScale(float device_pixels_per_point) {
  YGConfigSetPointScaleFactor(SharedLayoutConfig(), device_pixels_per_point);
}

float TextWrapWidth(const NodeStyle& style, float box_width) {
  return box_width - (style.shows_caret ? kCaretReservedWidth : 0.0f);
}

bool Rect::Contains(float point_x, float point_y) const {
  return point_x >= x && point_y >= y && point_x < x + width &&
         point_y < y + height;
}

Rect Rect::Intersect(const Rect& other) const {
  float left = std::max(x, other.x);
  float top = std::max(y, other.y);
  float right = std::min(x + width, other.x + other.width);
  float bottom = std::min(y + height, other.y + other.height);
  return {left, top, std::max(0.0f, right - left),
          std::max(0.0f, bottom - top)};
}

Node::Node(std::string id)
    : yoga_node_(YGNodeNewWithConfig(SharedLayoutConfig())),
      id_(std::move(id)) {
  YGNodeSetContext(yoga_node_, this);
}

Node::~Node() {
  for (auto& child : children_)
    YGNodeRemoveChild(yoga_node_, child->yoga_node_);
  YGNodeFree(yoga_node_);
}

Node* Node::AddChild(std::string id) {
  children_.push_back(std::make_unique<Node>(std::move(id)));
  Node* child = children_.back().get();
  YGNodeInsertChild(yoga_node_, child->yoga_node_,
                    YGNodeGetChildCount(yoga_node_));
  return child;
}

void Node::EnableTextMeasurement() {
  YGNodeSetMeasureFunc(yoga_node_, &MeasureTextNode);
}

void Node::ResolveBounds(float parent_x, float parent_y,
                         const Rect& parent_clip) {
  bounds_ = {parent_x + YGNodeLayoutGetLeft(yoga_node_),
             parent_y + YGNodeLayoutGetTop(yoga_node_),
             YGNodeLayoutGetWidth(yoga_node_),
             YGNodeLayoutGetHeight(yoga_node_)};
  visible_clip_ = parent_clip;
  Rect children_clip =
      style_.scrolls_vertically ? parent_clip.Intersect(bounds_) : parent_clip;
  float children_origin_y =
      bounds_.y - (style_.scrolls_vertically ? style_.scroll_offset : 0.0f);
  for (auto& child : children_) {
    child->ResolveBounds(bounds_.x, children_origin_y, children_clip);
  }
}

const Node* Node::HitTest(float x, float y) const {
  if (!visible_clip_.Contains(x, y) || !bounds_.Contains(x, y)) return nullptr;
  for (auto child = children_.rbegin(); child != children_.rend(); ++child) {
    if (const Node* hit = (*child)->HitTest(x, y)) return hit;
  }
  return id_.empty() ? nullptr : this;
}

Node* Node::FindById(std::string_view id) {
  if (id_ == id) return this;
  for (auto& child : children_) {
    if (Node* found = child->FindById(id)) return found;
  }
  return nullptr;
}

}
