#include "ui/Renderer.h"

#include <thorvg.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include "ui/TextEditing.h"

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

void DrawCaptionGlyph(const NodeStyle& style, const Rect& bounds,
                      tvg::Scene* target_scene) {
  constexpr float kGlyphHalfExtent = 5.0f;
  constexpr float kGlyphStrokeWidth = 1.0f;
  constexpr float kRestoreOffset = 2.0f;
  const float center_x = std::round(bounds.x + bounds.width / 2) + 0.5f;
  const float center_y = std::round(bounds.y + bounds.height / 2) + 0.5f;
  const float left = center_x - kGlyphHalfExtent;
  const float top = center_y - kGlyphHalfExtent;
  const float extent = 2 * kGlyphHalfExtent;
  tvg::Shape* glyph = tvg::Shape::gen();
  switch (style.icon) {
    case IconKind::kWindowMinimize:
      glyph->moveTo(left, center_y);
      glyph->lineTo(left + extent, center_y);
      break;
    case IconKind::kWindowMaximize:
      glyph->appendRect(left, top, extent, extent, 1, 1);
      break;
    case IconKind::kWindowRestore:
      glyph->appendRect(left, top + kRestoreOffset, extent - kRestoreOffset,
                        extent - kRestoreOffset, 1, 1);
      glyph->moveTo(left + kRestoreOffset, top);
      glyph->lineTo(left + extent, top);
      glyph->lineTo(left + extent, top + extent - kRestoreOffset);
      break;
    default:
      glyph->moveTo(left, top);
      glyph->lineTo(left + extent, top + extent);
      glyph->moveTo(left + extent, top);
      glyph->lineTo(left, top + extent);
      break;
  }
  glyph->strokeWidth(kGlyphStrokeWidth);
  glyph->strokeFill(style.icon_color.red, style.icon_color.green,
                    style.icon_color.blue, style.icon_color.alpha);
  target_scene->add(glyph);
}

void DrawRichText(const NodeStyle& style, const Rect& bounds,
                  tvg::Scene* target_scene) {
  constexpr float kBackgroundPadding = 3.0f;
  constexpr float kBackgroundCornerRadius = 4.0f;
  float line_top = bounds.y;
  for (const RichTextLine& line : LayoutRichText(style.spans, bounds.width)) {
    for (const RichTextFragment& fragment : line.fragments) {
      if (fragment.end_byte <= fragment.begin_byte) continue;
      const TextSpan& span = style.spans[fragment.span_index];
      const float text_top = line_top + line.ascent - span.font->ascent();
      if (IsVisible(span.background)) {
        tvg::Shape* background = MakeRoundedRect(
            {bounds.x + fragment.x - kBackgroundPadding, text_top,
             fragment.width + 2 * kBackgroundPadding, span.font->line_height()},
            kBackgroundCornerRadius);
        background->fill(span.background.red, span.background.green,
                         span.background.blue, span.background.alpha);
        target_scene->add(background);
      }
      std::string fragment_text = span.text.substr(
          fragment.begin_byte, fragment.end_byte - fragment.begin_byte);
      tvg::Text* text = tvg::Text::gen();
      text->font(span.font->font_key().empty() ? nullptr
                                               : span.font->font_key().c_str());
      text->size(span.font->point_size());
      text->text(fragment_text.c_str());
      text->fill(span.color.red, span.color.green, span.color.blue);
      if (span.color.alpha < 255) text->opacity(span.color.alpha);
      text->translate(bounds.x + fragment.x, text_top);
      target_scene->add(text);
    }
    line_top += line.height;
  }
}

void DrawIcon(const NodeStyle& style, const Rect& bounds,
              tvg::Scene* target_scene) {
  switch (style.icon) {
    case IconKind::kTulip:
      DrawTulip(style, bounds, target_scene);
      return;
    case IconKind::kStop: {
      constexpr float kStopHalfExtent = 4.5f;
      tvg::Shape* square =
          MakeRoundedRect({bounds.x + bounds.width / 2 - kStopHalfExtent,
                           bounds.y + bounds.height / 2 - kStopHalfExtent,
                           2 * kStopHalfExtent, 2 * kStopHalfExtent},
                          1.5f);
      square->fill(style.icon_color.red, style.icon_color.green,
                   style.icon_color.blue, style.icon_color.alpha);
      target_scene->add(square);
      return;
    }
    case IconKind::kWindowMinimize:
    case IconKind::kWindowMaximize:
    case IconKind::kWindowRestore:
    case IconKind::kWindowClose:
      DrawCaptionGlyph(style, bounds, target_scene);
      return;
    default:
      break;
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
    case IconKind::kArrowLeft:
      icon->moveTo(center_x + kIconHalfExtent + 1, center_y);
      icon->lineTo(center_x - kIconHalfExtent - 1, center_y);
      icon->moveTo(center_x - 1, center_y - kIconHalfExtent);
      icon->lineTo(center_x - kIconHalfExtent - 1, center_y);
      icon->lineTo(center_x - 1, center_y + kIconHalfExtent);
      break;
    case IconKind::kChevronDown:
      icon->moveTo(center_x - kIconHalfExtent + 1, center_y - 2);
      icon->lineTo(center_x, center_y + 2.5f);
      icon->lineTo(center_x + kIconHalfExtent - 1, center_y - 2);
      break;
    default:
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
  if (!style.spans.empty()) {
    DrawRichText(style, bounds, target_scene);
  } else if (style.font) {
    DrawText(node, target_scene, caret_visible);
  }
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

  if (!showing_placeholder && style.selection_end > style.selection_begin) {
    constexpr float kLineBreakSelectionWidth = 4.0f;
    for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
      const TextLine& line = lines[line_index];
      const size_t line_extent = line_index + 1 < lines.size()
                                     ? lines[line_index + 1].begin_byte
                                     : shown_text.size();
      const size_t begin = std::max(style.selection_begin, line.begin_byte);
      const size_t end = std::min(style.selection_end, line_extent);
      if (begin >= end) continue;
      const std::string_view line_text =
          std::string_view(shown_text)
              .substr(line.begin_byte, line.end_byte - line.begin_byte);
      const float left = font.Measure(line_text.substr(
          0, std::min(begin, line.end_byte) - line.begin_byte));
      float right = font.Measure(
          line_text.substr(0, std::min(end, line.end_byte) - line.begin_byte));
      if (end > line.end_byte && line_index + 1 < lines.size()) {
        right += kLineBreakSelectionWidth;
      }
      if (right <= left) continue;
      tvg::Shape* highlight = tvg::Shape::gen();
      highlight->appendRect(
          bounds.x + left,
          bounds.y + font.line_height() * static_cast<float>(line_index),
          right - left, font.line_height());
      highlight->fill(style.selection_color.red, style.selection_color.green,
                      style.selection_color.blue, style.selection_color.alpha);
      target_scene->add(highlight);
    }
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
      const CaretLocation location =
          LocateCaret(font, shown_text, lines,
                      std::min(style.caret_byte, shown_text.size()));
      caret_x += location.x;
      caret_y += font.line_height() * static_cast<float>(location.line_index);
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
