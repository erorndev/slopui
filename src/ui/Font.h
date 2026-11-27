#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tvg {
struct Text;
}

namespace lale::ui {

struct TextLine {
  size_t begin_byte = 0;
  size_t end_byte = 0;
  float width = 0;
};

class Font {
 public:
  Font(std::string font_key, float point_size);
  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  const std::string& font_key() const { return font_key_; }
  float point_size() const { return point_size_; }
  float line_height() const { return line_height_; }
  float ascent() const { return ascent_; }

  float Measure(std::string_view utf8_text) const;
  std::vector<TextLine> Wrap(std::string_view utf8_text,
                             float maximum_width) const;
  std::string Elide(std::string_view utf8_text, float maximum_width) const;

 private:
  float GlyphAdvance(std::string_view utf8_text, size_t byte_offset,
                     size_t* glyph_byte_length) const;

  std::string font_key_;
  float point_size_;
  float line_height_ = 0;
  float ascent_ = 0;
  tvg::Text* metrics_probe_ = nullptr;
  mutable std::unordered_map<uint32_t, float> glyph_advance_cache_;
};

std::string LoadFontFile(const std::string& font_path);

}
