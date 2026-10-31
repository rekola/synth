#ifndef _FLOATTEXT_H_
#define _FLOATTEXT_H_

#include <cellui/NumberParse.h>
#include <fmt/format.h>

#include <string>

// A float as the text a song file or a parameter bag keeps. Never the
// locale: a window toolkit sets it, and a decimal comma would write "0,5"
// and read "0.5" as 0.
namespace float_text {

// The shortest text that reads back as the same float.
inline std::string format(float value) { return fmt::format("{}", value); }

// The number at the start of `text`; 0 when there is none. Leading blanks and
// a '+' are allowed, as a hand-edited file may have them.
inline float parse(const char * text) {
  if (!text) return 0.0f;
  while (*text == ' ' || *text == '\t') text++;
  if (*text == '+') text++;
  const char * end = text;
  while (*end) end++;
  float value = 0.0f;
  if (cellui::parseFloat(text, end, value).ec != std::errc()) return 0.0f;
  return value;
}
inline float parse(const std::string & text) { return parse(text.c_str()); }

} // namespace float_text

#endif
