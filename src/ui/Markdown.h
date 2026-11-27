#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lale::ui {

struct MarkdownInline {
  std::string text;
  bool bold = false;
  bool italic = false;
  bool code = false;
  bool link = false;
};

using MarkdownInlines = std::vector<MarkdownInline>;

enum class MarkdownBlockKind {
  kParagraph,
  kHeading,
  kCodeBlock,
  kListItem,
  kQuote,
  kRule,
  kTable,
};

struct MarkdownBlock {
  MarkdownBlockKind kind = MarkdownBlockKind::kParagraph;
  int level = 0;
  bool ordered = false;
  int number = 0;
  std::string code;
  std::string language;
  MarkdownInlines inlines;
  std::vector<std::vector<MarkdownInlines>> table_rows;
};

std::vector<MarkdownBlock> ParseMarkdown(std::string_view markdown);
MarkdownInlines ParseMarkdownInlines(std::string_view text);

}
