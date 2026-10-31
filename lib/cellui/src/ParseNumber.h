#pragma once

#include <locale>
#include <sstream>
#include <string>

namespace cellui {

// The decimal number at the start of `text` (an optional sign, digits, a
// point, an exponent), whatever the global locale: a window toolkit sets it,
// and a decimal comma would read "0.7" as 0. `consumed` gets the characters
// used. False when `text` does not start with a number.
inline bool parseNumber(const std::string & text, float & value, size_t * consumed = nullptr) {
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  float v = 0.0f;
  if (!(in >> v)) return false;
  value = v;
  if (consumed) *consumed = in.eof() ? text.size() : static_cast<size_t>(in.tellg());
  return true;
}

}  // namespace cellui
