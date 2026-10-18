#ifndef _SCENENAME_H_
#define _SCENENAME_H_

#include "TimeSignature.h"

#include <cctype>
#include <string>

namespace scenename {

inline bool
validDenominator(int denominator) {
  return TimeSignature::validDenominator(denominator);
}

struct Parsed {
  std::string name;       // the text with the tempo and time signature removed
  bool has_tempo = false; // a tempo was typed (0 meaning "none")
  int tempo = 0;
  bool has_time_signature = false; // one was typed (numerator 0 meaning "none")
  int numerator = 0;
  int denominator = 0;
};

namespace detail {

inline bool
isDigit(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

// A number just before a "BPM" word, any case, 20-300; "0 BPM" or "- BPM"
// mean none. Sets [begin, end) to the span.
inline bool
findTempo(const std::string & text, size_t & begin, size_t & end, int & bpm) {
  auto lower = text;
  for (auto & c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (size_t at = lower.find("bpm"); at != std::string::npos; at = lower.find("bpm", at + 1)) {
    auto number_end = at;
    while (number_end > 0 && lower[number_end - 1] == ' ') number_end--;
    auto number_begin = number_end;
    while (number_begin > 0 && isDigit(lower[number_begin - 1])) number_begin--;
    bpm = 0;
    if (number_begin == number_end) {
      if (number_end == 0 || lower[number_end - 1] != '-') continue;
      number_begin = number_end - 1; // "- BPM"
    } else {
      if (number_end - number_begin > 3) continue;
      bpm = std::stoi(lower.substr(number_begin, number_end - number_begin));
      if (bpm != 0 && (bpm < 20 || bpm > 300)) continue;
    }
    begin = number_begin;
    end = at + 3;
    return true;
  }
  return false;
}

// "3/4", "6 / 8": up to 32 beats of a valid denominator; "0/4" means none.
inline bool
findTimeSignature(const std::string & text, size_t & begin, size_t & end, int & numerator, int & denominator) {
  for (size_t slash = text.find('/'); slash != std::string::npos; slash = text.find('/', slash + 1)) {
    auto left_end = slash;
    while (left_end > 0 && text[left_end - 1] == ' ') left_end--;
    auto left_begin = left_end;
    while (left_begin > 0 && isDigit(text[left_begin - 1])) left_begin--;
    auto right_begin = slash + 1;
    while (right_begin < text.size() && text[right_begin] == ' ') right_begin++;
    auto right_end = right_begin;
    while (right_end < text.size() && isDigit(text[right_end])) right_end++;
    if (left_end - left_begin < 1 || left_end - left_begin > 2 || right_end - right_begin < 1 || right_end - right_begin > 2) continue;
    auto num = std::stoi(text.substr(left_begin, left_end - left_begin));
    auto den = std::stoi(text.substr(right_begin, right_end - right_begin));
    if (!validDenominator(den) || num > 32) continue;
    begin = left_begin;
    end = right_end;
    numerator = num;
    denominator = den;
    return true;
  }
  return false;
}

// Trimmed, with what was either side of a removed token joined by one space.
inline std::string
tidy(const std::string & text) {
  auto first = text.find_first_not_of(' ');
  if (first == std::string::npos) return "";
  auto name = text.substr(first, text.find_last_not_of(' ') - first + 1);
  for (auto pos = name.find("  "); pos != std::string::npos; pos = name.find("  ")) name.erase(pos, 1);
  return name;
}

} // namespace detail

// Splits a typed scene name into its name, a tempo ("Waltz 90 BPM") and a
// time signature ("Waltz 3/4"). Anything that doesn't read as one stays in
// the name.
inline Parsed
extract(const std::string & text) {
  Parsed result;
  result.name = text;
  size_t begin = 0, end = 0;
  if (detail::findTempo(result.name, begin, end, result.tempo)) {
    result.has_tempo = true;
    result.name.erase(begin, end - begin);
    result.name.insert(begin, " ");
  }
  if (detail::findTimeSignature(result.name, begin, end, result.numerator, result.denominator)) {
    result.has_time_signature = true;
    result.name.erase(begin, end - begin);
    result.name.insert(begin, " ");
  }
  if (result.has_tempo || result.has_time_signature) result.name = detail::tidy(result.name);
  return result;
}

} // namespace scenename

#endif
