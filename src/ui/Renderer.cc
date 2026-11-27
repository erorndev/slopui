#include "ui/Renderer.h"

#include <thorvg.h>

#include <string>

namespace lale::ui {

namespace {

constexpr float kCaretWidth = 1.5f;

bool IsVisible(const Color& color) { return color.alpha > 0; }

tvg::Shape* MakeRoundedRect(const Rect& rect, float corner_radius) {
  tvg::Shape* shape = tvg::Shape::gen();
  shape->appendRect(rect.x, rect.y, rect.width, rect.height, corner_radius,
                    corner_radius);
  return shape;
}

void DrawIcon(const NodeStyle& style, const Rect& bounds,
              tvg::Scene* target_scene) {
  constexpr float kIconHalfExtent = 5.0f;
  constexpr float kIconStrokeWidth = 1.5f;
  const float center_x = bounds.x + bounds.width / 2;
  const float center_y = bounds.y + bounds.height / 2;
  tvg::Shape* icon = tvg::Shape::gen();
  switch (style.icon) {
    case IconKind::kMinus:
      icon->moveTo(center_x - kIconHalfExtent, center_y);
      icon->lineTo(center_x + kIconHalfExtent, center_y);
      break;
    case IconKind::kPlus:
      icon->moveTo(center_x - kIconHalfExtent, center_y);
      icon->lineTo(center_x + kIconHalfExtent, center_y);
      icon->moveTo(center_x, center_y - kIconHalfExtent);
      icon->lineTo(center_x, center_y + kIconHalfExtent);
      break;
    case IconKind::kClose:
      icon->moveTo(center_x - kIconHalfExtent, center_y - kIconHalfExtent);
      icon->lineTo(center_x + kIconHalfExtent, center_y + kIconHalfExtent);
      icon->moveTo(center_x + kIconHalfExtent, center_y - kIconHalfExtent);
      icon->lineTo(center_x - kIconHalfExtent, center_y + kIconHalfExtent);
      break;
    case IconKind::kNone:
      tvg::Paint::rel(icon);
      return;
  }
  icon->strokeWidth(kIconStrokeWidth);
  icon->strokeCap(tvg::StrokeCap::Round);
  icon->strokeFill(style.icon_color.red, style.icon_color.green,
                   style.icon_color.blue, style.icon_color.alpha);
  target_scene->add(icon);
}

}

Renderer::Renderer() : canvas_(tvg::SwCanvas::gen()) {}

Renderer::~Renderer() { delete canvas_; }

void Renderer::Resize(int width, int height) {
  if (width == width_ && height == height_) return;
  width_ = width;
  height_ = height;
  pixels_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
  canvas_->target(pixels_.data(), static_cast<uint32_t>(width),
                  static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                  tvg::ColorSpace::ARGB8888);
}

void Renderer::Render(const Node& root, bool caret_visible,
                      float device_pixels_per_point) {
  canvas_->remove();
  tvg::Scene* root_scene = tvg::Scene::gen();
  root_scene->scale(device_pixels_per_point);
  DrawNode(root, root_scene, caret_visible);
  canvas_->add(root_scene);
  canvas_->draw(true);
  canvas_->sync();
}

void Renderer::DrawNode(const Node& node, tvg::Scene* target_scene,
                        bool caret_visible) {
  const NodeStyle& style = node.style();
  const Rect& bounds = node.bounds();
  if (bounds.width <= 0 || bounds.height <= 0) return;
  if (node.visible_clip().Intersect(bounds).height <= 0) return;

  if (IsVisible(style.background)) {
    tvg::Shape* background = MakeRoundedRect(bounds, style.corner_radius);
    background->fill(style.background.red, style.background.green,
                     style.background.blue, style.background.alpha);
    target_scene->add(background);
  }
  if (IsVisible(style.border) && style.border_width > 0) {
    float inset = style.border_width / 2;
    Rect border_rect{bounds.x + inset, bounds.y + inset,
                     bounds.width - style.border_width,
                     bounds.height - style.border_width};
    tvg::Shape* border =
        MakeRoundedRect(border_rect, style.corner_radius - inset);
    border->strokeWidth(style.border_width);
    border->strokeFill(style.border.red, style.border.green, style.border.blue,
                       style.border.alpha);
    target_scene->add(border);
  }
  if (style.font) DrawText(node, target_scene, caret_visible);
  if (style.icon != IconKind::kNone) DrawIcon(style, bounds, target_scene);

  tvg::Scene* children_scene = target_scene;
  if (style.scrolls_vertically) {
    children_scene = tvg::Scene::gen();
    children_scene->clip(MakeRoundedRect(bounds, 0));
    target_scene->add(children_scene);
  }
  for (const auto& child : node.children()) {
    DrawNode(*child, children_scene, caret_visible);
  }
}

void Renderer::DrawText(const Node& node, tvg::Scene* target_scene,
                        bool caret_visible) {
  const NodeStyle& style = node.style();
  const Rect& bounds = node.bounds();
  const Font& font = *style.font;
  const bool showing_placeholder = style.text.empty();
  std::string shown_text = showing_placeholder ? style.placeholder : style.text;
  if (style.elides_text) shown_text = font.Elide(shown_text, bounds.width);
  const Color& color =
      showing_placeholder ? style.placeholder_color : style.text_color;

  std::vector<TextLine> lines;
  if (style.wraps_text) {
    lines = font.Wrap(shown_text, TextWrapWidth(style, bounds.width));
  } else {
    lines.push_back({0, shown_text.size(), font.Measure(shown_text)});
  }

  for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const TextLine& line = lines[line_index];
    if (line.end_byte <= line.begin_byte) continue;
    std::string line_text =
        shown_text.substr(line.begin_byte, line.end_byte - line.begin_byte);
    tvg::Text* text = tvg::Text::gen();
    text->font(font.font_key().empty() ? nullptr : font.font_key().c_str());
    text->size(font.point_size());
    text->text(line_text.c_str());
    text->fill(color.red, color.green, color.blue);
    if (color.alpha < 255) text->opacity(color.alpha);
    text->translate(bounds.x, bounds.y + font.line_height() *
                                             static_cast<float>(line_index));
    target_scene->add(text);
  }

  if (style.shows_caret && caret_visible) {
    float caret_x = bounds.x;
    float caret_y = bounds.y;
    if (!showing_placeholder && !lines.empty()) {
      caret_x += lines.back().width;
      caret_y += font.line_height() * static_cast<float>(lines.size() - 1);
    }
    tvg::Shape* caret = tvg::Shape::gen();
    caret->appendRect(caret_x, caret_y + 2, kCaretWidth,
                      font.line_height() - 4);
    caret->fill(style.text_color.red, style.text_color.green,
                style.text_color.blue, 255);
    target_scene->add(caret);
  }
}

}
