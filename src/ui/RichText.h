#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ui/Color.h"
#include "ui/Font.h"

namespace lale::ui {

struct TextSpan {
  std::string text;
  const Font* font = nullptr;
  Color color;
  Color background;
};

struct RichTextFragment {
  size_t span_index = 0;
  size_t begin_byte = 0;
  size_t end_byte = 0;
  float x = 0;
  float width = 0;
};

struct RichTextLine {
  std::vector<RichTextFragment> fragments;
  float width = 0;
  float ascent = 0;
  float height = 0;
};

std::vector<RichTextLine> LayoutRichText(const std::vector<TextSpan>& spans,
                                         float maximum_width);
std::vector<size_t> SpanStartOffsets(const std::vector<TextSpan>& spans);
std::string PlainTextOfSpans(const std::vector<TextSpan>& spans);
size_t RichTextOffsetAtPoint(const std::vector<TextSpan>& spans,
                             const std::vector<RichTextLine>& lines, float x,
                             float y);

}
