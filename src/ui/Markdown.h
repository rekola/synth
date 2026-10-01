#ifndef _MARKDOWN_H_
#define _MARKDOWN_H_

#include <string>
#include <vector>

// A deliberately tiny Markdown subset - just what the app's own info
// dialogs use, so the same text can be shown by any UI backend:
//   # Heading (1-6 #'s, one line)
//   paragraphs separated by blank lines (lines in one are joined)
//   *italic*, **bold**, and \ to escape a literal * or \ or #
// Anything else is plain text. An unmatched * stays a literal *.
namespace markdown {

struct Span {
  std::string text;
  bool bold = false;
  bool italic = false;
};

struct Block {
  enum class Kind { HEADING, PARAGRAPH };
  Kind kind = Kind::PARAGRAPH;
  int level = 0; // 1-6 for a heading
  std::vector<Span> spans;
};

using Document = std::vector<Block>;

Document parse(const std::string & source);

// Escapes `text` so parse() shows it literally - for dynamic text (a
// name, a description) put into a Markdown document.
std::string escape(const std::string & text);

// One piece of a laid-out line, in one style.
struct Run {
  std::string text;
  bool bold = false;
  bool italic = false;
};

using Line = std::vector<Run>;

// Wraps `doc` to `width` terminal columns: words never break (an
// over-long one gets a line of its own), a heading is all bold, and blocks
// are separated by an empty line. A graphical backend wraps by itself and
// only needs parse().
std::vector<Line> layout(const Document & doc, int width);

} // namespace markdown

#endif
