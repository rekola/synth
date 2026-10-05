#ifndef _SCENENAME_H_
#define _SCENENAME_H_

#include <cctype>
#include <string>

namespace scenename {

// The tempo a scene name asks for: the number just before a "BPM" word
// ("Waltz 90 BPM", "90bpm"), any case; 0 when there is none or it's out of
// range (20-300, what Controller::setTempo() accepts).
inline int
parseTempo(const std::string & name) {
  auto lower = name;
  for (auto & c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (size_t at = lower.find("bpm"); at != std::string::npos; at = lower.find("bpm", at + 1)) {
    auto end = at;
    while (end > 0 && lower[end - 1] == ' ') end--;
    auto begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(lower[begin - 1]))) begin--;
    if (begin == end || end - begin > 3) continue;
    auto bpm = std::stoi(lower.substr(begin, end - begin));
    if (bpm >= 20 && bpm <= 300) return bpm;
  }
  return 0;
}

} // namespace scenename

#endif
