#include "DrumRankTable.h"

#include <algorithm>
#include <array>

namespace DrumRankTable {

namespace {
  constexpr int kUnranked = -1;
  constexpr size_t kTableSize = 83; // covers GM note numbers 0..82

  const std::array<int, kTableSize> &
  rankTable() {
    static const std::array<int, kTableSize> table = [] {
      std::array<int, kTableSize> t;
      t.fill(kUnranked);
      int next = 0;
      auto assign = [&](std::initializer_list<int> notes) {
        for (int note : notes) t[static_cast<size_t>(note)] = next++;
      };
      assign({ 36, 35 });                             // 1. Bass drums
      assign({ 38, 40, 37, 39 });                      // 2. Snares, sticks, claps
      assign({ 41, 43, 45, 47, 48, 50 });               // 3. Toms, low to high
      assign({ 64, 62, 63, 61, 60, 66, 65 });           // 4. Latin hand drums, low to high
      assign({ 77, 76, 75, 74, 73, 69, 70, 82 });       // 5. Woods, shakers, scrapers (+ Shaker)
      assign({ 44, 42, 46 });                          // 6. Hi-hats, closed to open
      assign({ 56, 68, 67, 54 });                      // 7. Latin metals
      assign({ 51, 59, 53, 55, 52, 49, 57 });           // 8. Cymbals
      assign({ 58, 78, 79, 80, 81, 71, 72 });           // 9. Effects
      assign({ 27, 28, 29, 30, 31, 32, 33, 34 });       // 10. Electronic kit hits + metronome
      return t;
    }();
    return table;
  }
}

int
rankForNote(int note) {
  if (note < 0 || static_cast<size_t>(note) >= kTableSize) return kUnranked;
  return rankTable()[static_cast<size_t>(note)];
}

std::vector<int>
orderLanes(std::vector<int> notes) {
  std::sort(notes.begin(), notes.end(), [](int a, int b) {
    auto ra = rankForNote(a);
    auto rb = rankForNote(b);
    if (ra != rb) return ra < rb;
    return a < b; // stable fallback; not expected to matter, no ties
  });
  return notes;
}

}
