#include "ui/Markdown.h"

#include <algorithm>
#include <cctype>

namespace lale::ui {

namespace {

constexpr int kMaximumListDepth = 3;
constexpr int kSpacesPerListLevel = 2;

bool IsSpace(char character) { return character == ' ' || character == '\t'; }

bool IsWordCharacter(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0;
}

std::string_view Trim(std::string_view text) {
  size_t first = 0;
  while (first < text.size() && IsSpace(text[first])) ++first;
  size_t last = text.size();
  while (last > first && IsSpace(text[last - 1])) --last;
  return text.substr(first, last - first);
}

int LeadingSpaces(std::string_view line) {
  int count = 0;
  for (char character : line) {
    if (character == ' ') {
      ++count;
    } else if (character == '\t') {
      count += 4;
    } else {
      break;
    }
  }
  return count;
}

void AppendInline(MarkdownInline piece, MarkdownInlines* inlines) {
  if (piece.text.empty()) return;
  if (!inlines->empty()) {
    MarkdownInline& previous = inlines->back();
    if (previous.bold == piece.bold && previous.italic == piece.italic &&
        previous.code == piece.code && previous.link == piece.link) {
      previous.text += piece.text;
      return;
    }
  }
  inlines->push_back(std::move(piece));
}

size_t RunLength(std::string_view text, size_t offset, char character) {
  size_t length = 0;
  while (offset + length < text.size() && text[offset + length] == character) {
    ++length;
  }
  return length;
}

bool CanOpenEmphasis(std::string_view text, size_t offset, size_t length) {
  const char marker = text[offset];
  if (offset + length >= text.size()) return false;
  if (IsSpace(text[offset + length])) return false;
  if (marker == '_' && offset > 0 && IsWordCharacter(text[offset - 1])) {
    return false;
  }
  return true;
}

size_t FindEmphasisCloser(std::string_view text, size_t from, char marker,
                          size_t length) {
  size_t cursor = from;
  while (cursor < text.size()) {
    if (text[cursor] == '`') {
      const size_t ticks = RunLength(text, cursor, '`');
      const size_t close = text.find(std::string(ticks, '`'), cursor + ticks);
      cursor = close == std::string_view::npos ? cursor + ticks : close + ticks;
      continue;
    }
    if (text[cursor] == '\\') {
      cursor += 2;
      continue;
    }
    if (text[cursor] != marker) {
      ++cursor;
      continue;
    }
    const size_t run = RunLength(text, cursor, marker);
    const bool closes = cursor > from && !IsSpace(text[cursor - 1]) &&
                        (run == length || (length == 2 && run > 2) ||
                         (length == 1 && run == 3));
    const bool word_follows = marker == '_' && cursor + run < text.size() &&
                              IsWordCharacter(text[cursor + run]);
    if (closes && !word_follows) {
      return length == 2 && run > 2 ? cursor + run - 2 : cursor;
    }
    cursor += run;
  }
  return std::string_view::npos;
}

void ParseInlinesInto(std::string_view text, const MarkdownInline& style,
                      MarkdownInlines* inlines) {
  std::string literal;
  auto flush_literal = [&] {
    MarkdownInline piece = style;
    piece.text = std::move(literal);
    AppendInline(std::move(piece), inlines);
    literal.clear();
  };
  size_t cursor = 0;
  while (cursor < text.size()) {
    const char character = text[cursor];
    if (character == '\\' && cursor + 1 < text.size() &&
        std::ispunct(static_cast<unsigned char>(text[cursor + 1]))) {
      literal.push_back(text[cursor + 1]);
      cursor += 2;
      continue;
    }
    if (character == '`') {
      const size_t ticks = RunLength(text, cursor, '`');
      const size_t close = text.find(std::string(ticks, '`'), cursor + ticks);
      if (close != std::string_view::npos) {
        flush_literal();
        std::string_view code =
            text.substr(cursor + ticks, close - cursor - ticks);
        if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ') {
          code = code.substr(1, code.size() - 2);
        }
        MarkdownInline piece = style;
        piece.code = true;
        piece.text = std::string(code);
        AppendInline(std::move(piece), inlines);
        cursor = close + ticks;
        continue;
      }
      literal.append(ticks, '`');
      cursor += ticks;
      continue;
    }
    if (character == '*' || character == '_') {
      const size_t run = RunLength(text, cursor, character);
      if (run >= 2 && CanOpenEmphasis(text, cursor, 2)) {
        const size_t close = FindEmphasisCloser(text, cursor + 2, character, 2);
        if (close != std::string_view::npos) {
          flush_literal();
          MarkdownInline inner = style;
          inner.bold = true;
          ParseInlinesInto(text.substr(cursor + 2, close - cursor - 2), inner,
                           inlines);
          cursor = close + 2;
          continue;
        }
      }
      if (CanOpenEmphasis(text, cursor, 1)) {
        const size_t close = FindEmphasisCloser(text, cursor + 1, character, 1);
        if (close != std::string_view::npos) {
          flush_literal();
          MarkdownInline inner = style;
          inner.italic = true;
          ParseInlinesInto(text.substr(cursor + 1, close - cursor - 1), inner,
                           inlines);
          cursor = close + 1;
          continue;
        }
      }
      literal.append(run, character);
      cursor += run;
      continue;
    }
    if (character == '~' && RunLength(text, cursor, '~') == 2) {
      const size_t close = text.find("~~", cursor + 2);
      if (close != std::string_view::npos) {
        flush_literal();
        ParseInlinesInto(text.substr(cursor + 2, close - cursor - 2), style,
                         inlines);
        cursor = close + 2;
        continue;
      }
    }
    if (character == '[') {
      const size_t close_bracket = text.find(']', cursor + 1);
      if (close_bracket != std::string_view::npos &&
          close_bracket + 1 < text.size() && text[close_bracket + 1] == '(') {
        const size_t close_paren = text.find(')', close_bracket + 2);
        if (close_paren != std::string_view::npos) {
          flush_literal();
          MarkdownInline inner = style;
          inner.link = true;
          ParseInlinesInto(text.substr(cursor + 1, close_bracket - cursor - 1),
                           inner, inlines);
          cursor = close_paren + 1;
          continue;
        }
      }
    }
    literal.push_back(character);
    ++cursor;
  }
  flush_literal();
}

bool ParseFence(std::string_view line, char* fence_character,
                size_t* fence_length, std::string* language) {
  if (LeadingSpaces(line) > 3) return false;
  std::string_view trimmed = Trim(line);
  if (trimmed.empty() || (trimmed[0] != '`' && trimmed[0] != '~')) {
    return false;
  }
  const size_t run = RunLength(trimmed, 0, trimmed[0]);
  if (run < 3) return false;
  *fence_character = trimmed[0];
  *fence_length = run;
  if (language) language->assign(Trim(trimmed.substr(run)));
  return true;
}

int HeadingLevel(std::string_view line, std::string_view* content) {
  if (LeadingSpaces(line) > 3) return 0;
  std::string_view trimmed = Trim(line);
  const size_t hashes = RunLength(trimmed, 0, '#');
  if (hashes == 0 || hashes > 6) return 0;
  if (hashes < trimmed.size() && !IsSpace(trimmed[hashes])) return 0;
  std::string_view rest = Trim(trimmed.substr(hashes));
  while (!rest.empty() && rest.back() == '#') rest.remove_suffix(1);
  *content = Trim(rest);
  return static_cast<int>(hashes);
}

bool IsRule(std::string_view line) {
  std::string_view trimmed = Trim(line);
  if (trimmed.empty()) return false;
  const char marker = trimmed[0];
  if (marker != '-' && marker != '*' && marker != '_') return false;
  int count = 0;
  for (char character : trimmed) {
    if (character == marker) {
      ++count;
    } else if (!IsSpace(character)) {
      return false;
    }
  }
  return count >= 3;
}

bool ParseListMarker(std::string_view line, int* indent, bool* ordered,
                     int* number, std::string_view* content) {
  *indent = LeadingSpaces(line);
  std::string_view rest = line.substr(line.find_first_not_of(" \t"));
  if (rest.size() >= 2 &&
      (rest[0] == '-' || rest[0] == '*' || rest[0] == '+') &&
      IsSpace(rest[1])) {
    *ordered = false;
    *content = Trim(rest.substr(2));
    return true;
  }
  size_t digits = 0;
  while (digits < rest.size() && digits < 9 &&
         std::isdigit(static_cast<unsigned char>(rest[digits]))) {
    ++digits;
  }
  if (digits == 0 || digits + 1 >= rest.size()) return false;
  if ((rest[digits] != '.' && rest[digits] != ')') ||
      !IsSpace(rest[digits + 1])) {
    return false;
  }
  *ordered = true;
  *number = std::stoi(std::string(rest.substr(0, digits)));
  *content = Trim(rest.substr(digits + 2));
  return true;
}

bool IsTableSeparator(std::string_view line) {
  std::string_view trimmed = Trim(line);
  if (trimmed.empty()) return false;
  bool has_dash = false;
  for (char character : trimmed) {
    if (character == '-') {
      has_dash = true;
    } else if (character != '|' && character != ':' && !IsSpace(character)) {
      return false;
    }
  }
  return has_dash && trimmed.find('|') != std::string_view::npos;
}

std::vector<MarkdownInlines> ParseTableRow(std::string_view line) {
  std::string_view trimmed = Trim(line);
  if (!trimmed.empty() && trimmed.front() == '|') trimmed.remove_prefix(1);
  if (!trimmed.empty() && trimmed.back() == '|') trimmed.remove_suffix(1);
  std::vector<MarkdownInlines> cells;
  std::string cell;
  for (size_t index = 0; index < trimmed.size(); ++index) {
    if (trimmed[index] == '\\' && index + 1 < trimmed.size() &&
        trimmed[index + 1] == '|') {
      cell.push_back('|');
      ++index;
    } else if (trimmed[index] == '|') {
      cells.push_back(ParseMarkdownInlines(Trim(cell)));
      cell.clear();
    } else {
      cell.push_back(trimmed[index]);
    }
  }
  cells.push_back(ParseMarkdownInlines(Trim(cell)));
  return cells;
}

bool StartsNewBlock(std::string_view line) {
  std::string_view unused_content;
  char unused_fence = 0;
  size_t unused_length = 0;
  int unused_indent = 0;
  bool unused_ordered = false;
  int unused_number = 0;
  return ParseFence(line, &unused_fence, &unused_length, nullptr) ||
         HeadingLevel(line, &unused_content) > 0 || IsRule(line) ||
         Trim(line).starts_with('>') ||
         ParseListMarker(line, &unused_indent, &unused_ordered, &unused_number,
                         &unused_content);
}

void AppendJoinedLine(std::string_view line, std::string* text) {
  const bool hard_break = line.ends_with("  ") || line.ends_with('\\');
  std::string_view content = Trim(line);
  if (content.ends_with('\\')) content.remove_suffix(1);
  if (!text->empty() && text->back() != '\n') text->push_back(' ');
  text->append(content);
  if (hard_break) text->push_back('\n');
}

}

MarkdownInlines ParseMarkdownInlines(std::string_view text) {
  MarkdownInlines inlines;
  ParseInlinesInto(text, MarkdownInline{}, &inlines);
  return inlines;
}

std::vector<MarkdownBlock> ParseMarkdown(std::string_view markdown) {
  std::vector<std::string_view> lines;
  size_t line_begin = 0;
  while (line_begin <= markdown.size()) {
    size_t line_end = markdown.find('\n', line_begin);
    if (line_end == std::string_view::npos) line_end = markdown.size();
    std::string_view line = markdown.substr(line_begin, line_end - line_begin);
    if (line.ends_with('\r')) line.remove_suffix(1);
    lines.push_back(line);
    line_begin = line_end + 1;
  }

  std::vector<MarkdownBlock> blocks;
  std::string paragraph;
  auto flush_paragraph = [&] {
    if (paragraph.empty()) return;
    while (!paragraph.empty() && paragraph.back() == '\n') paragraph.pop_back();
    MarkdownBlock block;
    block.inlines = ParseMarkdownInlines(paragraph);
    blocks.push_back(std::move(block));
    paragraph.clear();
  };

  size_t index = 0;
  while (index < lines.size()) {
    const std::string_view line = lines[index];
    char fence_character = 0;
    size_t fence_length = 0;
    std::string language;
    if (ParseFence(line, &fence_character, &fence_length, &language)) {
      flush_paragraph();
      MarkdownBlock block{MarkdownBlockKind::kCodeBlock};
      block.language = std::move(language);
      ++index;
      std::vector<std::string_view> code_lines;
      while (index < lines.size()) {
        char closing_character = 0;
        size_t closing_length = 0;
        std::string closing_info;
        if (ParseFence(lines[index], &closing_character, &closing_length,
                       &closing_info) &&
            closing_character == fence_character &&
            closing_length >= fence_length && closing_info.empty()) {
          ++index;
          break;
        }
        code_lines.push_back(lines[index]);
        ++index;
      }
      for (size_t code_index = 0; code_index < code_lines.size();
           ++code_index) {
        if (code_index > 0) block.code.push_back('\n');
        block.code.append(code_lines[code_index]);
      }
      blocks.push_back(std::move(block));
      continue;
    }
    if (Trim(line).empty()) {
      flush_paragraph();
      ++index;
      continue;
    }
    std::string_view heading_content;
    if (const int level = HeadingLevel(line, &heading_content)) {
      flush_paragraph();
      MarkdownBlock block{MarkdownBlockKind::kHeading};
      block.level = level;
      block.inlines = ParseMarkdownInlines(heading_content);
      blocks.push_back(std::move(block));
      ++index;
      continue;
    }
    const std::string_view trimmed = Trim(line);
    if (!paragraph.empty() && (RunLength(trimmed, 0, '=') == trimmed.size() ||
                               RunLength(trimmed, 0, '-') == trimmed.size())) {
      MarkdownBlock block{MarkdownBlockKind::kHeading};
      block.level = trimmed[0] == '=' ? 1 : 2;
      block.inlines = ParseMarkdownInlines(paragraph);
      paragraph.clear();
      blocks.push_back(std::move(block));
      ++index;
      continue;
    }
    if (IsRule(line)) {
      flush_paragraph();
      blocks.push_back({MarkdownBlockKind::kRule});
      ++index;
      continue;
    }
    if (line.find('|') != std::string_view::npos && index + 1 < lines.size() &&
        IsTableSeparator(lines[index + 1])) {
      flush_paragraph();
      MarkdownBlock block{MarkdownBlockKind::kTable};
      block.table_rows.push_back(ParseTableRow(line));
      index += 2;
      while (index < lines.size() && !Trim(lines[index]).empty() &&
             lines[index].find('|') != std::string_view::npos) {
        block.table_rows.push_back(ParseTableRow(lines[index]));
        ++index;
      }
      blocks.push_back(std::move(block));
      continue;
    }
    if (trimmed.starts_with('>')) {
      flush_paragraph();
      std::string quote;
      while (index < lines.size() && Trim(lines[index]).starts_with('>')) {
        std::string_view content = Trim(lines[index]).substr(1);
        if (content.starts_with(' ')) content.remove_prefix(1);
        if (Trim(content).empty()) {
          if (!quote.empty()) quote.push_back('\n');
        } else {
          AppendJoinedLine(content, &quote);
        }
        ++index;
      }
      while (!quote.empty() && quote.back() == '\n') quote.pop_back();
      MarkdownBlock block{MarkdownBlockKind::kQuote};
      block.inlines = ParseMarkdownInlines(quote);
      blocks.push_back(std::move(block));
      continue;
    }
    int indent = 0;
    bool ordered = false;
    int number = 0;
    std::string_view item_content;
    if (ParseListMarker(line, &indent, &ordered, &number, &item_content)) {
      flush_paragraph();
      std::string item_text(item_content);
      ++index;
      while (index < lines.size() && !Trim(lines[index]).empty() &&
             !StartsNewBlock(lines[index])) {
        AppendJoinedLine(lines[index], &item_text);
        ++index;
      }
      MarkdownBlock block{MarkdownBlockKind::kListItem};
      block.level = std::min(indent / kSpacesPerListLevel, kMaximumListDepth);
      block.ordered = ordered;
      block.number = number;
      block.inlines = ParseMarkdownInlines(item_text);
      blocks.push_back(std::move(block));
      continue;
    }
    AppendJoinedLine(line, &paragraph);
    ++index;
  }
  flush_paragraph();
  return blocks;
}

}
