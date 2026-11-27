#include "ui/Font.h"

#include <thorvg.h>

#include <algorithm>

namespace lale::ui {

namespace {

constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
constexpr float kWrapTolerance = 0.5f;

size_t DecodeUtf8(std::string_view text, size_t offset, uint32_t* code_point) {
  auto lead = static_cast<unsigned char>(text[offset]);
  size_t length = 1;
  if (lead >= 0xF0) {
    length = 4;
  } else if (lead >= 0xE0) {
    length = 3;
  } else if (lead >= 0xC0) {
    length = 2;
  }
  length = std::min(length, text.size() - offset);
  uint32_t value = length == 1 ? lead : lead & (0x7F >> length);
  for (size_t i = 1; i < length; ++i) {
    value =
        (value << 6) | (static_cast<unsigned char>(text[offset + i]) & 0x3F);
  }
  *code_point = value;
  return length;
}

}

std::string LoadFontFile(const std::string& font_path) {
  if (tvg::Text::load(font_path.c_str()) != tvg::Result::Success) return {};
  size_t last_separator = font_path.find_last_of("/\\");
  std::string file_name = last_separator == std::string::npos
                              ? font_path
                              : font_path.substr(last_separator + 1);
  size_t extension_dot = file_name.find_last_of('.');
  return extension_dot == std::string::npos
             ? file_name
             : file_name.substr(0, extension_dot);
}

Font::Font(std::string font_key, float point_size)
    : font_key_(std::move(font_key)), point_size_(point_size) {
  metrics_probe_ = tvg::Text::gen();
  metrics_probe_->ref();
  metrics_probe_->font(font_key_.empty() ? nullptr : font_key_.c_str());
  metrics_probe_->size(point_size_);
  tvg::TextMetrics text_metrics{};
  if (metrics_probe_->metrics(text_metrics) == tvg::Result::Success) {
    line_height_ = text_metrics.advance;
    ascent_ = text_metrics.ascent;
  }
  if (line_height_ <= 0) line_height_ = point_size_ * 1.3f;
  if (ascent_ <= 0) ascent_ = point_size_;
}

Font::~Font() { metrics_probe_->unref(); }

float Font::GlyphAdvance(std::string_view utf8_text, size_t byte_offset,
                         size_t* glyph_byte_length) const {
  uint32_t code_point = 0;
  *glyph_byte_length = DecodeUtf8(utf8_text, byte_offset, &code_point);
  auto cached = glyph_advance_cache_.find(code_point);
  if (cached != glyph_advance_cache_.end()) return cached->second;

  std::string glyph(utf8_text.substr(byte_offset, *glyph_byte_length));
  tvg::GlyphMetrics glyph_metrics{};
  float advance = metrics_probe_->metrics(glyph.c_str(), glyph_metrics) ==
                          tvg::Result::Success
                      ? glyph_metrics.advance
                      : point_size_ * 0.5f;
  glyph_advance_cache_.emplace(code_point, advance);
  return advance;
}

float Font::Measure(std::string_view utf8_text) const {
  float total_width = 0;
  size_t offset = 0;
  while (offset < utf8_text.size()) {
    size_t length = 0;
    total_width += GlyphAdvance(utf8_text, offset, &length);
    offset += length;
  }
  return total_width;
}

std::vector<TextLine> Font::Wrap(std::string_view utf8_text,
                                 float maximum_width) const {
  std::vector<TextLine> lines;
  const float space_width = Measure(" ");
  size_t paragraph_begin = 0;
  while (true) {
    size_t newline = utf8_text.find('\n', paragraph_begin);
    size_t paragraph_end =
        newline == std::string_view::npos ? utf8_text.size() : newline;

    size_t line_begin = paragraph_begin;
    if (line_begin == paragraph_end) {
      lines.push_back({line_begin, line_begin, 0});
    }
    while (line_begin < paragraph_end) {
      float line_width = 0;
      size_t last_break = std::string_view::npos;
      float width_at_last_break = 0;
      size_t cursor = line_begin;
      while (cursor < paragraph_end) {
        size_t glyph_length = 0;
        float advance = GlyphAdvance(utf8_text, cursor, &glyph_length);
        const bool hanging_space = utf8_text[cursor] == ' ';
        if (!hanging_space &&
            line_width + advance > maximum_width + kWrapTolerance &&
            cursor > line_begin) {
          break;
        }
        line_width += advance;
        cursor += glyph_length;
        if (utf8_text[cursor - 1] == ' ') {
          last_break = cursor;
          width_at_last_break = line_width;
        }
      }
      bool overflowed = cursor < paragraph_end;
      if (overflowed && last_break != std::string_view::npos &&
          last_break > line_begin) {
        lines.push_back({line_begin, last_break - 1,
                         std::max(0.0f, width_at_last_break - space_width)});
        line_begin = last_break;
      } else {
        lines.push_back({line_begin, cursor, line_width});
        line_begin = cursor;
      }
    }
    if (newline == std::string_view::npos) break;
    paragraph_begin = newline + 1;
  }
  return lines;
}

std::string Font::Elide(std::string_view utf8_text, float maximum_width) const {
  if (Measure(utf8_text) <= maximum_width) return std::string(utf8_text);
  const float ellipsis_width = Measure(kEllipsis);
  float used_width = 0;
  size_t offset = 0;
  while (offset < utf8_text.size()) {
    size_t length = 0;
    float advance = GlyphAdvance(utf8_text, offset, &length);
    if (used_width + advance + ellipsis_width > maximum_width) break;
    used_width += advance;
    offset += length;
  }
  return std::string(utf8_text.substr(0, offset)) + std::string(kEllipsis);
}

}
