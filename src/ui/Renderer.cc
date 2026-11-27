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

void BoxBlurLine(const uint32_t* source, uint32_t* destination, int length,
                 int destination_stride, int radius) {
  const int window = 2 * radius + 1;
  int sums[4] = {};
  auto channel = [](uint32_t pixel, int index) {
    return static_cast<int>((pixel >> (index * 8)) & 0xFF);
  };
  auto sample = [&](int position) {
    return source[static_cast<size_t>(std::clamp(position, 0, length - 1))];
  };
  for (int offset = -radius; offset <= radius; ++offset) {
    const uint32_t pixel = sample(offset);
    for (int index = 0; index < 4; ++index)
      sums[index] += channel(pixel, index);
  }
  for (int position = 0; position < length; ++position) {
    uint32_t blurred = 0;
    for (int index = 0; index < 4; ++index) {
      blurred |= static_cast<uint32_t>(sums[index] / window) << (index * 8);
    }
    destination[static_cast<size_t>(position) * destination_stride] = blurred;
    const uint32_t leaving = sample(position - radius);
    const uint32_t entering = sample(position + radius + 1);
    for (int index = 0; index < 4; ++index) {
      sums[index] += channel(entering, index) - channel(leaving, index);
    }
  }
}

void BoxBlurRows(std::vector<uint32_t>* region, int width, int height,
                 int radius) {
  std::vector<uint32_t> line(static_cast<size_t>(width));
  for (int row = 0; row < height; ++row) {
    uint32_t* row_start = region->data() + static_cast<size_t>(row) * width;
    std::copy_n(row_start, width, line.data());
    BoxBlurLine(line.data(), row_start, width, 1, radius);
  }
}

void BoxBlurColumns(std::vector<uint32_t>* region, int width, int height,
                    int radius) {
  std::vector<uint32_t> line(static_cast<size_t>(height));
  for (int column = 0; column < width; ++column) {
    for (int row = 0; row < height; ++row) {
      line[static_cast<size_t>(row)] =
          (*region)[static_cast<size_t>(row) * width + column];
    }
    BoxBlurLine(line.data(), region->data() + column, height, width, radius);
  }
}

uint32_t BlendOver(uint32_t source, uint32_t destination) {
  const uint32_t source_alpha = source >> 24;
  if (source_alpha == 255) return source;
  if (source_alpha == 0) return destination;
  const uint32_t remaining = 255 - source_alpha;
  uint32_t blended = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    const uint32_t source_channel = (source >> shift) & 0xFF;
    const uint32_t destination_channel = (destination >> shift) & 0xFF;
    const uint32_t channel = std::min<uint32_t>(
        255, source_channel + (destination_channel * remaining + 127) / 255);
    blended |= channel << shift;
  }
  return blended;
}

bool InsideRoundedRect(float x, float y, float width, float height,
                       float corner_radius) {
  const float nearest_x = std::clamp(x, corner_radius, width - corner_radius);
  const float nearest_y = std::clamp(y, corner_radius, height - corner_radius);
  const float delta_x = x - nearest_x;
  const float delta_y = y - nearest_y;
  return delta_x * delta_x + delta_y * delta_y <= corner_radius * corner_radius;
}

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

void DrawGradient(const NodeStyle& style, const Rect& bounds,
                  tvg::Scene* target_scene) {
  std::vector<tvg::Fill::ColorStop> color_stops;
  color_stops.reserve(style.background_gradient.size());
  for (const GradientStop& stop : style.background_gradient) {
    color_stops.push_back({stop.offset, stop.color.red, stop.color.green,
                           stop.color.blue, stop.color.alpha});
  }
  const float period =
      style.gradient_period > 0 ? style.gradient_period : bounds.width;
  const float start_x =
      bounds.x + std::fmod(style.gradient_offset, period) - period;
  tvg::LinearGradient* gradient = tvg::LinearGradient::gen();
  gradient->linear(start_x, bounds.y, start_x + period, bounds.y);
  gradient->colorStops(color_stops.data(),
                       static_cast<uint32_t>(color_stops.size()));
  gradient->spread(tvg::FillSpread::Repeat);
  tvg::Shape* shape = MakeRoundedRect(bounds, style.corner_radius);
  shape->fill(gradient);
  target_scene->add(shape);
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
  const bool has_selection = style.selection_end > style.selection_begin &&
                             style.selection_end != std::string::npos;
  const std::vector<size_t> span_offsets = SpanStartOffsets(style.spans);
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
      if (has_selection) {
        const size_t span_start = span_offsets[fragment.span_index];
        const size_t begin =
            std::clamp(style.selection_begin, span_start + fragment.begin_byte,
                       span_start + fragment.end_byte) -
            span_start;
        const size_t end =
            std::clamp(style.selection_end, span_start + fragment.begin_byte,
                       span_start + fragment.end_byte) -
            span_start;
        if (end > begin) {
          const std::string_view span_text = span.text;
          const float left = span.font->Measure(span_text.substr(
              fragment.begin_byte, begin - fragment.begin_byte));
          const float right = span.font->Measure(
              span_text.substr(fragment.begin_byte, end - fragment.begin_byte));
          tvg::Shape* highlight = tvg::Shape::gen();
          highlight->appendRect(bounds.x + fragment.x + left, line_top,
                                right - left, line.height);
          highlight->fill(
              style.selection_color.red, style.selection_color.green,
              style.selection_color.blue, style.selection_color.alpha);
          target_scene->add(highlight);
        }
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
  const Node* backdrop_node = FindBackdropNode(root);
  if (!backdrop_node) {
    DrawNode(root, root_scene, caret_visible);
    canvas_->add(root_scene);
    canvas_->draw(true);
    canvas_->sync();
    return;
  }
  DrawNodesBefore(root, *backdrop_node, root_scene);
  canvas_->add(root_scene);
  canvas_->draw(true);
  canvas_->sync();
  BlurBehind(*backdrop_node, device_pixels_per_point);
  backdrop_pixels_ = pixels_;
  canvas_->remove();
  tvg::Scene* overlay_scene = tvg::Scene::gen();
  overlay_scene->scale(device_pixels_per_point);
  bool reached = false;
  DrawNodesFrom(root, *backdrop_node, overlay_scene, caret_visible, &reached);
  canvas_->add(overlay_scene);
  canvas_->draw(true);
  canvas_->sync();
  for (size_t index = 0; index < pixels_.size(); ++index) {
    pixels_[index] = BlendOver(pixels_[index], backdrop_pixels_[index]);
  }
}

void Renderer::DrawNode(const Node& node, tvg::Scene* target_scene,
                        bool caret_visible) {
  const Rect& bounds = node.bounds();
  if (bounds.width <= 0 || bounds.height <= 0) return;
  if (node.visible_clip().Intersect(bounds).height <= 0) return;
  DrawNodeContents(node, target_scene, caret_visible);
  tvg::Scene* children_scene = target_scene;
  if (node.style().scrolls_vertically) {
    children_scene = tvg::Scene::gen();
    children_scene->clip(MakeRoundedRect(bounds, 0));
    target_scene->add(children_scene);
  }
  for (const auto& child : node.children()) {
    DrawNode(*child, children_scene, caret_visible);
  }
}

bool Renderer::DrawNodesBefore(const Node& node, const Node& stop_node,
                               tvg::Scene* target_scene) {
  if (&node == &stop_node) return true;
  const Rect& bounds = node.bounds();
  if (bounds.width <= 0 || bounds.height <= 0) return false;
  if (node.visible_clip().Intersect(bounds).height <= 0) return false;
  DrawNodeContents(node, target_scene, false);
  tvg::Scene* children_scene = target_scene;
  if (node.style().scrolls_vertically) {
    children_scene = tvg::Scene::gen();
    children_scene->clip(MakeRoundedRect(bounds, 0));
    target_scene->add(children_scene);
  }
  for (const auto& child : node.children()) {
    if (DrawNodesBefore(*child, stop_node, children_scene)) return true;
  }
  return false;
}

void Renderer::DrawNodesFrom(const Node& node, const Node& start_node,
                             tvg::Scene* target_scene, bool caret_visible,
                             bool* reached) {
  if (*reached || &node == &start_node) {
    *reached = true;
    DrawNode(node, target_scene, caret_visible);
    return;
  }
  const Rect& bounds = node.bounds();
  if (bounds.width <= 0 || bounds.height <= 0) return;
  tvg::Scene* children_scene = target_scene;
  if (node.style().scrolls_vertically) {
    children_scene = tvg::Scene::gen();
    children_scene->clip(MakeRoundedRect(bounds, 0));
    target_scene->add(children_scene);
  }
  for (const auto& child : node.children()) {
    DrawNodesFrom(*child, start_node, children_scene, caret_visible, reached);
  }
}

void Renderer::BlurBehind(const Node& node, float device_pixels_per_point) {
  const NodeStyle& style = node.style();
  const Rect& bounds = node.bounds();
  const int left = std::max(
      0, static_cast<int>(std::floor(bounds.x * device_pixels_per_point)));
  const int top = std::max(
      0, static_cast<int>(std::floor(bounds.y * device_pixels_per_point)));
  const int right =
      std::min(width_, static_cast<int>(std::ceil((bounds.x + bounds.width) *
                                                  device_pixels_per_point)));
  const int bottom =
      std::min(height_, static_cast<int>(std::ceil((bounds.y + bounds.height) *
                                                   device_pixels_per_point)));
  if (right <= left || bottom <= top) return;
  const int radius = std::max(
      1, static_cast<int>(style.backdrop_blur / 2 * device_pixels_per_point));
  const int region_width = right - left;
  const int region_height = bottom - top;
  std::vector<uint32_t> region(static_cast<size_t>(region_width) *
                               static_cast<size_t>(region_height));
  for (int row = 0; row < region_height; ++row) {
    std::copy_n(&pixels_[static_cast<size_t>(top + row) * width_ + left],
                region_width, &region[static_cast<size_t>(row) * region_width]);
  }
  constexpr int kBoxBlurPasses = 3;
  for (int pass = 0; pass < kBoxBlurPasses; ++pass) {
    BoxBlurRows(&region, region_width, region_height, radius);
    BoxBlurColumns(&region, region_width, region_height, radius);
  }
  const float corner_radius = style.corner_radius * device_pixels_per_point;
  for (int row = 0; row < region_height; ++row) {
    for (int column = 0; column < region_width; ++column) {
      if (!InsideRoundedRect(
              column + 0.5f, row + 0.5f, static_cast<float>(region_width),
              static_cast<float>(region_height), corner_radius)) {
        continue;
      }
      pixels_[static_cast<size_t>(top + row) * width_ + left + column] =
          region[static_cast<size_t>(row) * region_width + column];
    }
  }
}

const Node* Renderer::FindBackdropNode(const Node& node) {
  if (node.style().backdrop_blur > 0 && node.bounds().width > 0 &&
      node.bounds().height > 0) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const Node* found = FindBackdropNode(*child)) return found;
  }
  return nullptr;
}

void Renderer::DrawNodeContents(const Node& node, tvg::Scene* target_scene,
                                bool caret_visible) {
  const NodeStyle& style = node.style();
  const Rect& bounds = node.bounds();
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
  if (!style.background_gradient.empty()) {
    DrawGradient(style, bounds, target_scene);
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
