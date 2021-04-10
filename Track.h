#ifndef _TRACK_H_
#define _TRACK_H_

#include <vector>

class Track {
 public:
  explicit Track() { }

  int getPattern(size_t i) const { return i < pattern.size() ? pattern[i] : 255; }
  void addPattern(int p) { pattern.push_back(p); }
  size_t size() const { return pattern.size(); }

private:
  std::vector<int> pattern;
};

#endif
