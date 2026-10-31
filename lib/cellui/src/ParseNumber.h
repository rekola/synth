#pragma once

#include <cctype>
#include <locale>
#include <sstream>
#include <string>

namespace cellui {

// The decimal number at the start of `text` (an optional sign, digits, a
// point, an exponent), whatever the global locale: a window toolkit sets it,
// and a decimal comma would read "0.7" as 0. `consumed` gets the characters
// used. False when `text` does not start with a number.
inline bool parseNumber(const std::string & text, float & value, size_t * consumed = nullptr) {
  // The extent is found here: a stream reads further than the number when a
  // letter after it is a hex digit ("60deg"), and then fails.
  auto digits = [&](size_t at) {
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) at++;
    return at;
  };
  size_t end = (!text.empty() && (text[0] == '+' || text[0] == '-')) ? 1 : 0;
  size_t whole = digits(end);
  size_t fraction = whole;
  if (whole < text.size() && text[whole] == '.') fraction = digits(whole + 1);
  if (whole == end && fraction <= whole + 1) return false;  // no digit at all
  end = fraction;
  if (end < text.size() && (text[end] == 'e' || text[end] == 'E')) {
    size_t at = end + 1;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) at++;
    size_t exponent = digits(at);
    if (exponent > at) end = exponent;
  }
  std::istringstream in(text.substr(0, end));
  in.imbue(std::locale::classic());
  float v = 0.0f;
  if (!(in >> v)) return false;
  value = v;
  if (consumed) *consumed = end;
  return true;
}

}  // namespace cellui
