#ifndef _SCENENAME_H_
#define _SCENENAME_H_

#include <cctype>
#include <string>

namespace scenename {

struct Parsed {
  std::string name; // the text with the tempo removed
  bool has_tempo = false; // a tempo was typed (0 meaning "none")
  int tempo = 0;
};

// Splits a typed scene name into its name and a tempo: a number just before
// a "BPM" word ("Waltz 90 BPM", "90bpm"), any case, 20-300; "0 BPM" or
// "- BPM" clear the tempo. Out-of-range numbers stay in the name.
inline Parsed
extract(const std::string & text) {
  auto lower = text;
  for (auto & c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  Parsed result;
  result.name = text;
  for (size_t at = lower.find("bpm"); at != std::string::npos; at = lower.find("bpm", at + 1)) {
    auto end = at;
    while (end > 0 && lower[end - 1] == ' ') end--;
    auto begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(lower[begin - 1]))) begin--;
    int bpm = 0;
    if (begin == end) {
      if (end == 0 || lower[end - 1] != '-') continue;
      begin = end - 1; // "- BPM"
    } else {
      if (end - begin > 3) continue;
      bpm = std::stoi(lower.substr(begin, end - begin));
      if (bpm != 0 && (bpm < 20 || bpm > 300)) continue;
    }
    result.has_tempo = true;
    result.tempo = bpm;
    auto name = text.substr(0, begin) + " " + text.substr(at + 3);
    // Trim, and join what was either side of the tempo with a single space.
    auto first = name.find_first_not_of(' ');
    auto last = name.find_last_not_of(' ');
    name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
    for (auto pos = name.find("  "); pos != std::string::npos; pos = name.find("  ")) name.erase(pos, 1);
    result.name = name;
    break;
  }
  return result;
}

} // namespace scenename

#endif
