#include "ui/TextEditing.h"

#include <algorithm>
#include <cctype>

namespace lale::ui {

namespace {

constexpr size_t kMaximumUndoSnapshots = 200;

bool IsContinuationByte(char character) {
  return (static_cast<unsigned char>(character) & 0xC0) == 0x80;
}

bool IsWordByte(char character) {
  const auto value = static_cast<unsigned char>(character);
  return value >= 0x80 || std::isalnum(value) != 0 || character == '_';
}

size_t LineIndexForByte(const std::vector<TextLine>& lines, size_t byte) {
  size_t line_index = 0;
  for (size_t index = 1; index < lines.size(); ++index) {
    if (lines[index].begin_byte > byte) break;
    line_index = index;
  }
  return line_index;
}

}

void TextEditHistory::Record(const std::string& text,
                             const TextSelection& selection, bool is_typing) {
  const bool merges = is_typing && last_edit_was_typing_;
  last_edit_was_typing_ = is_typing;
  if (merges) return;
  undo_snapshots_.push_back({text, selection});
  if (undo_snapshots_.size() > kMaximumUndoSnapshots) {
    undo_snapshots_.erase(undo_snapshots_.begin());
  }
  redo_snapshots_.clear();
}

bool TextEditHistory::Undo(std::string* text, TextSelection* selection) {
  if (undo_snapshots_.empty()) return false;
  redo_snapshots_.push_back({*text, *selection});
  *text = std::move(undo_snapshots_.back().text);
  *selection = undo_snapshots_.back().selection;
  undo_snapshots_.pop_back();
  last_edit_was_typing_ = false;
  return true;
}

bool TextEditHistory::Redo(std::string* text, TextSelection* selection) {
  if (redo_snapshots_.empty()) return false;
  undo_snapshots_.push_back({*text, *selection});
  *text = std::move(redo_snapshots_.back().text);
  *selection = redo_snapshots_.back().selection;
  redo_snapshots_.pop_back();
  last_edit_was_typing_ = false;
  return true;
}

void TextEditHistory::Clear() {
  undo_snapshots_.clear();
  redo_snapshots_.clear();
  last_edit_was_typing_ = false;
}

std::vector<TextLine> WrapForEditing(const Font& font, std::string_view text,
                                     float wrap_width) {
  std::vector<TextLine> lines = font.Wrap(text, wrap_width);
  if (lines.empty()) lines.push_back({0, 0, 0});
  return lines;
}

CaretLocation LocateCaret(const Font& font, std::string_view text,
                          const std::vector<TextLine>& lines, size_t byte) {
  const size_t line_index = LineIndexForByte(lines, byte);
  const TextLine& line = lines[line_index];
  const size_t clamped = std::clamp(byte, line.begin_byte, line.end_byte);
  return {line_index, font.Measure(text.substr(line.begin_byte,
                                               clamped - line.begin_byte))};
}

size_t ByteAtPoint(const Font& font, std::string_view text,
                   const std::vector<TextLine>& lines, float x, float y) {
  const float line_height = font.line_height();
  const auto line_index = static_cast<size_t>(
      std::clamp(y / line_height, 0.0f, static_cast<float>(lines.size() - 1)));
  const TextLine& line = lines[line_index];
  float advance_sum = 0;
  size_t cursor = line.begin_byte;
  while (cursor < line.end_byte) {
    size_t glyph_length = 0;
    const float advance = font.GlyphAdvance(text, cursor, &glyph_length);
    if (x < advance_sum + advance / 2) return cursor;
    advance_sum += advance;
    cursor += glyph_length;
  }
  return line.end_byte;
}

size_t VisualLineStart(const std::vector<TextLine>& lines, size_t byte) {
  return lines[LineIndexForByte(lines, byte)].begin_byte;
}

size_t VisualLineEnd(std::string_view text, const std::vector<TextLine>& lines,
                     size_t byte) {
  return std::min(lines[LineIndexForByte(lines, byte)].end_byte, text.size());
}

size_t PreviousCharacter(std::string_view text, size_t byte) {
  if (byte == 0) return 0;
  --byte;
  while (byte > 0 && IsContinuationByte(text[byte])) --byte;
  return byte;
}

size_t NextCharacter(std::string_view text, size_t byte) {
  if (byte >= text.size()) return text.size();
  ++byte;
  while (byte < text.size() && IsContinuationByte(text[byte])) ++byte;
  return byte;
}

size_t PreviousWordStart(std::string_view text, size_t byte) {
  while (byte > 0 && !IsWordByte(text[byte - 1])) --byte;
  while (byte > 0 && IsWordByte(text[byte - 1])) --byte;
  return byte;
}

size_t NextWordEnd(std::string_view text, size_t byte) {
  while (byte < text.size() && !IsWordByte(text[byte])) ++byte;
  while (byte < text.size() && IsWordByte(text[byte])) ++byte;
  return byte;
}

void ClampSelection(std::string_view text, TextSelection* selection) {
  selection->caret = std::min(selection->caret, text.size());
  selection->anchor = std::min(selection->anchor, text.size());
}

void MoveCaret(TextSelection* selection, size_t byte, bool extend) {
  selection->caret = byte;
  if (!extend) selection->anchor = byte;
}

void ReplaceSelection(std::string* text, TextSelection* selection,
                      std::string_view replacement) {
  ClampSelection(*text, selection);
  const size_t begin = selection->begin();
  text->replace(begin, selection->end() - begin, replacement);
  selection->caret = begin + replacement.size();
  selection->anchor = selection->caret;
}

std::string SelectedText(std::string_view text,
                         const TextSelection& selection) {
  const size_t begin = std::min(selection.begin(), text.size());
  const size_t end = std::min(selection.end(), text.size());
  return std::string(text.substr(begin, end - begin));
}

}
