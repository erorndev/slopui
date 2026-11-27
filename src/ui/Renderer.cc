#include "ui/Renderer.h"

#include <thorvg.h>

#include <algorithm>
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

void DrawShadows(const NodeStyle& style, const Rect& bounds,
                 tvg::Scene* target_scene) {
  constexpr int kBlurQuality = 75;
  for (const Shadow& shadow : style.shadows) {
    if (!IsVisible(shadow.color)) continue;
    Rect shadow_rect{
        bounds.x - shadow.spread, bounds.y - shadow.spread + shadow.offset_y,
        bounds.width + 2 * shadow.spread, bounds.height + 2 * shadow.spread};
    if (shadow_rect.width <= 0 || shadow_rect.height <= 0) continue;
    tvg::Shape* shape = MakeRoundedRect(
        shadow_rect, std::max(0.0f, style.corner_radius + shadow.spread));
    shape->fill(shadow.color.red, shadow.color.green, shadow.color.blue,
                shadow.color.alpha);
    if (shadow.blur <= 0) {
      target_scene->add(shape);
      continue;
    }
    tvg::Scene* blurred = tvg::Scene::gen();
    blurred->add(shape);
    blurred->add(tvg::SceneEffect::GaussianBlur,
                 static_cast<double>(shadow.blur / 2), 0, 0, kBlurQuality);
    target_scene->add(blurred);
  }
}

void DrawOuterRing(const NodeStyle& style, const Rect& bounds,
                   tvg::Scene* target_scene) {
  const float half_width = style.outer_ring_width / 2;
  Rect ring_rect{bounds.x - half_width, bounds.y - half_width,
                 bounds.width + style.outer_ring_width,
                 bounds.height + style.outer_ring_width};
  tvg::Shape* ring =
      MakeRoundedRect(ring_rect, style.corner_radius + half_width);
  ring->strokeWidth(style.outer_ring_width);
  ring->strokeFill(style.outer_ring.red, style.outer_ring.green,
                   style.outer_ring.blue, style.outer_ring.alpha);
  target_scene->add(ring);
}

void DrawTulip(const NodeStyle& style, const Rect& bounds,
               tvg::Scene* target_scene) {
  constexpr float kDesignSize = 20.0f;
  const float scale = std::min(bounds.width, bounds.height) / kDesignSize;
  const float origin_x = bounds.x + (bounds.width - kDesignSize * scale) / 2;
  const float origin_y = bounds.y + (bounds.height - kDesignSize * scale) / 2;
  auto point_x = [&](float design_x) { return origin_x + design_x * scale; };
  auto point_y = [&](float design_y) { return origin_y + design_y * scale; };

  tvg::Shape* bloom = tvg::Shape::gen();
  bloom->moveTo(point_x(4), point_y(5));
  bloom->lineTo(point_x(7), point_y(8));
  bloom->lineTo(point_x(10), point_y(3));
  bloom->lineTo(point_x(13), point_y(8));
  bloom->lineTo(point_x(16), point_y(5));
  bloom->cubicTo(point_x(16.5f), point_y(10.5f), point_x(14), point_y(14),
                 point_x(10), point_y(14));
  bloom->cubicTo(point_x(6), point_y(14), point_x(3.5f), point_y(10.5f),
                 point_x(4), point_y(5));
  bloom->close();
  bloom->fill(style.icon_color.red, style.icon_color.green,
              style.icon_color.blue, style.icon_color.alpha);
  target_scene->add(bloom);

  tvg::Shape* stem = tvg::Shape::gen();
  stem->moveTo(point_x(10), point_y(14));
  stem->lineTo(point_x(10), point_y(18.5f));
  stem->moveTo(point_x(10), point_y(17.5f));
  stem->cubicTo(point_x(11), point_y(15.5f), point_x(12.5f), point_y(15),
                point_x(14), point_y(15));
  stem->strokeWidth(1.6f * scale);
  stem->strokeCap(tvg::StrokeCap::Round);
  stem->strokeFill(style.icon_color.red, style.icon_color.green,
                   style.icon_color.blue, style.icon_color.alpha);
  target_scene->add(stem);
}

void DrawIcon(const NodeStyle& style, const Rect& bounds,
              tvg::Scene* target_scene) {
  if (style.icon == IconKind::kTulip) {
    DrawTulip(style, bounds, target_scene);
    return;
  }
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
    case IconKind::kArrowUp:
      icon->moveTo(center_x, center_y + kIconHalfExtent + 1);
      icon->lineTo(center_x, center_y - kIconHalfExtent - 1);
      icon->moveTo(center_x - kIconHalfExtent, center_y - 1);
      icon->lineTo(center_x, center_y - kIconHalfExtent - 1);
      icon->lineTo(center_x + kIconHalfExtent, center_y - 1);
      break;
    case IconKind::kChevronDown:
      icon->moveTo(center_x - kIconHalfExtent + 1, center_y - 2);
      icon->lineTo(center_x, center_y + 2.5f);
      icon->lineTo(center_x + kIconHalfExtent - 1, center_y - 2);
      break;
    case IconKind::kNone:
    case IconKind::kTulip:
      tvg::Paint::rel(icon);
      return;
  }
  icon->strokeJoin(tvg::StrokeJoin::Round);
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

  if (!style.shadows.empty()) DrawShadows(style, bounds, target_scene);
  if (IsVisible(style.outer_ring) && style.outer_ring_width > 0) {
    DrawOuterRing(style, bounds, target_scene);
  }
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
