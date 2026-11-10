#ifndef _VELOCITY_H_
#define _VELOCITY_H_

#include <algorithm>
#include <cmath>

// Velocities are 7-bit, 0-127. Velocity 0 is a real note-on that starts from
// silence.
namespace velocity {

constexpr int kMax7 = 127;

// The note gain, in dB, of a velocity-0 note-on: low enough that the linear gain
// is exactly zero in float. The note starts from true silence.
constexpr float kSilentGainDb = -1000.0f;

constexpr float toFloat(int v7) { return static_cast<float>(std::clamp(v7, 0, kMax7)) / 127.0f; }

// A level in [0,1] to its 7-bit velocity (what a velocity zone is chosen by).
inline int floatTo7(float level) { return static_cast<int>(std::lround(std::clamp(level, 0.0f, 1.0f) * 127.0f)); }

// The release velocity ("lift") scales the release time, fixed when the OFF
// arrives: 64 leaves it alone, 0 makes it four times longer, 127 about four
// times shorter.
inline float releaseTimeFactor(int lift7) {
  return std::exp2(static_cast<float>(64 - std::clamp(lift7, 0, kMax7)) / 32.0f);
}

// The same scaling for an SF2 release generator: (64 - lift7) * 37.5
// timecents added, held to the range the SF2 spec allows (-12000 to 8000).
// `seconds` is the generator's release already converted from timecents; a
// zero (a pinned, effectively instant release) stays zero.
constexpr float kSf2ReleaseMinTimecents = -12000.0f;
constexpr float kSf2ReleaseMaxTimecents = 8000.0f;
inline float sf2ReleaseSeconds(float seconds, int lift7) {
  lift7 = std::clamp(lift7, 0, kMax7);
  if (lift7 == 64 || !(seconds > 0.0f)) return seconds;
  const float lo = std::exp2(kSf2ReleaseMinTimecents / 1200.0f);
  const float hi = std::exp2(kSf2ReleaseMaxTimecents / 1200.0f);
  const float timecents_offset = static_cast<float>(64 - lift7) * 37.5f;
  return std::clamp(seconds * std::exp2(timecents_offset / 1200.0f), lo, hi);
}

} // namespace velocity

#endif
