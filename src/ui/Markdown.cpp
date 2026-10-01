#include "Markdown.h"

#include "../util/Utf8.h"

#include <algorithm>

using namespace std;

namespace markdown {

namespace {

bool isBlank(const string & line) {
  return line.find_first_not_of(" \t\r") == string::npos;
}

// Position of the first unescaped `marker` at or after `from`, or npos.
size_t findClosing(const string & s, const string & marker, size_t from) {
  for (size_t i = from; i < s.size(); i++) {
    if (s[i] == '\\') i++;
    else if (s.compare(i, marker.size(), marker) == 0) return i;
  }
  return string::npos;
}

void appendSpan(vector<Span> & out, const string & text, bool bold, bool italic) {
  if (text.empty()) return;
  if (!out.empty() && out.back().bold == bold && out.back().italic == italic) out.back().text += text;
  else out.push_back({ text, bold, italic });
}

void parseInline(const string & s, bool bold, bool italic, vector<Span> & out) {
  string plain;
  size_t i = 0;
  while (i < s.size()) {
    char c = s[i];
    if (c == '\\' && i + 1 < s.size()) {
      plain += s[i + 1];
      i += 2;
      continue;
    }
    if (c == '*') {
      bool strong = s.compare(i, 2, "**") == 0;
      size_t open = strong ? 2 : 1;
      auto close = findClosing(s, strong ? "**" : "*", i + open);
      if (close != string::npos && close > i + open) {
	appendSpan(out, plain, bold, italic);
	plain.clear();
	parseInline(s.substr(i + open, close - i - open), bold || strong, italic || !strong, out);
	i = close + open;
	continue;
      }
    }
    plain += c;
    i++;
  }
  appendSpan(out, plain, bold, italic);
}

// Joins a paragraph's lines into one, single-spaced.
string joinLines(const vector<string> & lines) {
  string joined;
  for (auto & line : lines) {
    auto begin = line.find_first_not_of(" \t\r");
    auto end = line.find_last_not_of(" \t\r");
    if (!joined.empty()) joined += ' ';
    joined += line.substr(begin, end - begin + 1);
  }
  return joined;
}

// The heading level of `line` ("## Foo" -> 2), or 0 if it isn't one.
int headingLevel(const string & line) {
  size_t n = 0;
  while (n < line.size() && line[n] == '#') n++;
  if (n < 1 || n > 6 || n >= line.size() || line[n] != ' ') return 0;
  return static_cast<int>(n);
}

} // namespace

Document parse(const string & source) {
  Document doc;
  vector<string> paragraph;

  auto flush = [&]() {
    if (paragraph.empty()) return;
    Block block;
    parseInline(joinLines(paragraph), false, false, block.spans);
    doc.push_back(std::move(block));
    paragraph.clear();
  };

  size_t pos = 0;
  while (pos <= source.size()) {
    auto end = source.find('\n', pos);
    if (end == string::npos) end = source.size();
    auto line = source.substr(pos, end - pos);
    pos = end + 1;

    if (isBlank(line)) {
      flush();
    } else if (auto level = headingLevel(line)) {
      flush();
      Block block;
      block.kind = Block::Kind::HEADING;
      block.level = level;
      auto text = line.substr(static_cast<size_t>(level) + 1);
      parseInline(joinLines({ text }), false, false, block.spans);
      doc.push_back(std::move(block));
    } else {
      paragraph.push_back(line);
    }
  }
  flush();
  return doc;
}

string escape(const string & text) {
  string out;
  for (char c : text) {
    if (c == '*' || c == '\\') out += '\\';
    out += c;
  }
  // A leading # would otherwise read as a heading.
  if (!out.empty() && out[0] == '#') out.insert(0, "\\");
  return out;
}

namespace {

struct Word {
  string text;
  bool bold, italic;
  bool space_before; // whitespace separated it from the previous word
};

vector<Word> splitWords(const vector<Span> & spans, bool force_bold) {
  vector<Word> words;
  bool pending_space = false;
  for (auto & span : spans) {
    bool bold = span.bold || force_bold;
    size_t i = 0;
    while (i < span.text.size()) {
      if (span.text[i] == ' ') {
	pending_space = true;
	i++;
	continue;
      }
      auto end = span.text.find(' ', i);
      if (end == string::npos) end = span.text.size();
      words.push_back({ span.text.substr(i, end - i), bold, span.italic, pending_space });
      pending_space = false;
      i = end;
    }
  }
  return words;
}

void appendRun(Line & line, const string & text, bool bold, bool italic) {
  if (!line.empty() && line.back().bold == bold && line.back().italic == italic) line.back().text += text;
  else line.push_back({ text, bold, italic });
}

int lineWidth(const Line & line) {
  int width = 0;
  for (auto & run : line) width += Utf8::displayWidth(run.text);
  return width;
}

} // namespace

vector<Line> layout(const Document & doc, int width) {
  vector<Line> lines;
  if (width <= 0) return lines;

  for (auto & block : doc) {
    if (!lines.empty()) lines.emplace_back();
    Line current;
    for (auto & word : splitWords(block.spans, block.kind == Block::Kind::HEADING)) {
      auto word_width = Utf8::displayWidth(word.text);
      bool space = word.space_before && !current.empty();
      if (!current.empty() && lineWidth(current) + (space ? 1 : 0) + word_width > width) {
	lines.push_back(std::move(current));
	current = Line();
	space = false;
      }
      if (space) appendRun(current, " ", false, false);
      appendRun(current, word.text, word.bold, word.italic);
    }
    if (!current.empty()) lines.push_back(std::move(current));
  }
  return lines;
}

} // namespace markdown
