#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ui/Font.h"

namespace lale::ui {

struct TextSelection {
  size_t caret = 0;
  size_t anchor = 0;

  size_t begin() const { return caret < anchor ? caret : anchor; }
  size_t end() const { return caret < anchor ? anchor : caret; }
  bool empty() const { return caret == anchor; }
};

struct CaretLocation {
  size_t line_index = 0;
  float x = 0;
};

class TextEditHistory {
 public:
  void Record(const std::string& text, const TextSelection& selection,
              bool is_typing);
  bool Undo(std::string* text, TextSelection* selection);
  bool Redo(std::string* text, TextSelection* selection);
  void Clear();

 private:
  struct Snapshot {
    std::string text;
    TextSelection selection;
  };

  std::vector<Snapshot> undo_snapshots_;
  std::vector<Snapshot> redo_snapshots_;
  bool last_edit_was_typing_ = false;
};

std::vector<TextLine> WrapForEditing(const Font& font, std::string_view text,
                                     float wrap_width);
CaretLocation LocateCaret(const Font& font, std::string_view text,
                          const std::vector<TextLine>& lines, size_t byte);
size_t ByteAtPoint(const Font& font, std::string_view text,
                   const std::vector<TextLine>& lines, float x, float y);
size_t VisualLineStart(const std::vector<TextLine>& lines, size_t byte);
size_t VisualLineEnd(std::string_view text, const std::vector<TextLine>& lines,
                     size_t byte);

size_t PreviousCharacter(std::string_view text, size_t byte);
size_t NextCharacter(std::string_view text, size_t byte);
size_t PreviousWordStart(std::string_view text, size_t byte);
size_t NextWordEnd(std::string_view text, size_t byte);

void ClampSelection(std::string_view text, TextSelection* selection);
void MoveCaret(TextSelection* selection, size_t byte, bool extend);
void ReplaceSelection(std::string* text, TextSelection* selection,
                      std::string_view replacement);
std::string SelectedText(std::string_view text, const TextSelection& selection);

}
