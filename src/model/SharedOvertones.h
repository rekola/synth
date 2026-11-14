#ifndef _SHAREDOVERTONES_H_
#define _SHAREDOVERTONES_H_

#include "JustIntonation.h"

#include <climits>
#include <cstdint>
#include <numeric>

// Which overtones of a sounding note are exactly overtones of a target note
// too (0Hxx). With the target at p/q times the sounding note, in lowest
// terms, they are the sounding note's p-th, 2p-th, 3p-th and so on. The
// ratio comes from the notes' keys (their gamut ratios, octaves included),
// never from the frequencies they sound at, so tuning corrections do not
// change it. Without a gamut ratio for both notes only whole octaves match.
namespace shared_overtones {

// The furthest apart two notes may be, in octaves, for p to stay exact.
inline constexpr int kMaxOctaves = 30;

// p, the step between shared overtones: 1 when every overtone is shared (the
// target at the sounding note or an octave below it), 0 when none is.
inline int step(const just_intonation::ScaleCorrections & scale, int edo, int sounding_note, int target_note) {
  auto sounding = scale.ratioForNote(sounding_note);
  auto target = scale.ratioForNote(target_note);
  if (sounding && target) {
    const int octaves = target->octaves - sounding->octaves;
    if (octaves > kMaxOctaves || octaves < -kMaxOctaves) return 0;
    std::int64_t num = std::int64_t{target->ratio.num} * sounding->ratio.den;
    std::int64_t den = std::int64_t{target->ratio.den} * sounding->ratio.num;
    if (octaves >= 0)
      num <<= octaves;
    else
      den <<= -octaves;
    const std::int64_t p = num / std::gcd(num, den);
    return p > INT_MAX ? 0 : static_cast<int>(p);
  }
  if (edo <= 0) return 0;
  const int apart = target_note - sounding_note;
  if (apart % edo != 0) return 0;
  const int octaves = apart / edo;
  if (octaves <= 0) return 1;
  return octaves > kMaxOctaves ? 0 : 1 << octaves;
}

} // namespace shared_overtones

#endif
