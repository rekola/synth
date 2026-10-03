#ifndef _SWING_H_
#define _SWING_H_

// Song-level swing: the second note of every eighth-note pair (4 rows, a row
// being a sixteenth) is played late. The amount is the share of the pair
// taken by its first note, in percent.
namespace swing {

constexpr int kStraight = 50;
constexpr int kMax = 75;
constexpr int kPairRows = 4;

inline int clamp(int percent) {
  return percent < kStraight ? kStraight : (percent > kMax ? kMax : percent);
}

// How many rows later than its grid position the note on `row` sounds: only
// the second eighth of a pair moves, by up to a whole row at kMax.
inline float offsetRows(int row, int percent) {
  int in_pair = ((row % kPairRows) + kPairRows) % kPairRows;
  if (in_pair != kPairRows / 2) return 0.0f;
  return (static_cast<float>(clamp(percent)) / 100.0f - 0.5f) * static_cast<float>(kPairRows);
}

} // namespace swing

#endif
