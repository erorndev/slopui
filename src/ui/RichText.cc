#include "ui/RichText.h"

#include <algorithm>

namespace lale::ui {

namespace {

constexpr float kWrapTolerance = 0.5f;

struct TextAtom {
  size_t span_index = 0;
  size_t begin_byte = 0;
  size_t end_byte = 0;
  float width = 0;
  float trailing_space_width = 0;
  bool ends_word = false;
  bool breaks_line = false;
};

std::vector<TextAtom> SplitIntoAtoms(const std::vector<TextSpan>& spans) {
  std::vector<TextAtom> atoms;
  for (size_t span_index = 0; span_index < spans.size(); ++span_index) {
    const TextSpan& span = spans[span_index];
    if (!span.font) continue;
    const std::string_view text = span.text;
    size_t cursor = 0;
    while (cursor < text.size()) {
      TextAtom atom{span_index, cursor, cursor};
      while (cursor < text.size() && text[cursor] != ' ' &&
             text[cursor] != '\n') {
        size_t glyph_length = 0;
        atom.width += span.font->GlyphAdvance(text, cursor, &glyph_length);
        cursor += glyph_length;
      }
      atom.end_byte = cursor;
      while (cursor < text.size() && text[cursor] == ' ') {
        size_t glyph_length = 0;
        atom.trailing_space_width +=
            span.font->GlyphAdvance(text, cursor, &glyph_length);
        cursor += glyph_length;
        atom.ends_word = true;
      }
      if (cursor < text.size() && text[cursor] == '\n') {
        atom.breaks_line = true;
        atom.ends_word = true;
        ++cursor;
      }
      atoms.push_back(atom);
    }
  }
  if (!atoms.empty()) atoms.back().ends_word = true;
  return atoms;
}

class LineBuilder {
 public:
  LineBuilder(const std::vector<TextSpan>& spans, float maximum_width)
      : spans_(spans), maximum_width_(maximum_width) {}

  bool fits(float width) const {
    return line_.fragments.empty() ||
           cursor_x_ + width <= maximum_width_ + kWrapTolerance;
  }
  bool empty() const { return line_.fragments.empty(); }
  float remaining_width() const { return maximum_width_ - cursor_x_; }

  void Place(size_t span_index, size_t begin_byte, size_t end_byte, float width,
             float trailing_space_width) {
    const Font* font = spans_[span_index].font;
    line_.ascent = std::max(line_.ascent, font->ascent());
    descent_ = std::max(descent_, font->line_height() - font->ascent());
    if (!line_.fragments.empty()) {
      RichTextFragment& previous = line_.fragments.back();
      if (previous.span_index == span_index &&
          previous.end_byte <= begin_byte) {
        previous.end_byte = end_byte;
        previous.width = cursor_x_ + width - previous.x;
        cursor_x_ += width + trailing_space_width;
        line_.width = previous.x + previous.width;
        return;
      }
    }
    line_.fragments.push_back(
        {span_index, begin_byte, end_byte, cursor_x_, width});
    line_.width = cursor_x_ + width;
    cursor_x_ += width + trailing_space_width;
  }

  void Finish(std::vector<RichTextLine>* lines, const Font* fallback_font) {
    if (line_.fragments.empty() && fallback_font) {
      line_.ascent = fallback_font->ascent();
      descent_ = fallback_font->line_height() - fallback_font->ascent();
    }
    line_.height = line_.ascent + descent_;
    lines->push_back(std::move(line_));
    line_ = {};
    descent_ = 0;
    cursor_x_ = 0;
  }

 private:
  const std::vector<TextSpan>& spans_;
  float maximum_width_;
  RichTextLine line_;
  float descent_ = 0;
  float cursor_x_ = 0;
};

void PlaceAtomByGlyph(const std::vector<TextSpan>& spans, const TextAtom& atom,
                      LineBuilder* builder, std::vector<RichTextLine>* lines) {
  const TextSpan& span = spans[atom.span_index];
  const std::string_view text = span.text;
  size_t piece_begin = atom.begin_byte;
  float piece_width = 0;
  size_t cursor = atom.begin_byte;
  while (cursor < atom.end_byte) {
    size_t glyph_length = 0;
    const float advance = span.font->GlyphAdvance(text, cursor, &glyph_length);
    if (piece_width + advance > builder->remaining_width() + kWrapTolerance &&
        (cursor > piece_begin || !builder->empty())) {
      if (cursor > piece_begin) {
        builder->Place(atom.span_index, piece_begin, cursor, piece_width, 0);
      }
      builder->Finish(lines, span.font);
      piece_begin = cursor;
      piece_width = 0;
    }
    piece_width += advance;
    cursor += glyph_length;
  }
  builder->Place(atom.span_index, piece_begin, atom.end_byte, piece_width,
                 atom.trailing_space_width);
}

}

std::vector<RichTextLine> LayoutRichText(const std::vector<TextSpan>& spans,
                                         float maximum_width) {
  std::vector<RichTextLine> lines;
  const std::vector<TextAtom> atoms = SplitIntoAtoms(spans);
  const Font* first_font = nullptr;
  for (const TextSpan& span : spans) {
    if (span.font) {
      first_font = span.font;
      break;
    }
  }
  LineBuilder builder(spans, maximum_width);
  size_t word_begin = 0;
  while (word_begin < atoms.size()) {
    size_t word_end = word_begin;
    float word_width = 0;
    while (true) {
      word_width += atoms[word_end].width;
      if (atoms[word_end].ends_word) break;
      word_width += atoms[word_end].trailing_space_width;
      ++word_end;
    }
    if (!builder.fits(word_width)) {
      builder.Finish(&lines, spans[atoms[word_begin].span_index].font);
    }
    const bool overflows_alone = word_width > maximum_width + kWrapTolerance;
    for (size_t index = word_begin; index <= word_end; ++index) {
      const TextAtom& atom = atoms[index];
      if (overflows_alone) {
        PlaceAtomByGlyph(spans, atom, &builder, &lines);
      } else {
        builder.Place(atom.span_index, atom.begin_byte, atom.end_byte,
                      atom.width, atom.trailing_space_width);
      }
    }
    if (atoms[word_end].breaks_line) {
      builder.Finish(&lines, spans[atoms[word_end].span_index].font);
    }
    word_begin = word_end + 1;
  }
  if (!builder.empty() || lines.empty()) builder.Finish(&lines, first_font);
  return lines;
}

}
