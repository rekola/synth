#ifndef _VELOCITY_H_
#define _VELOCITY_H_

#include <algorithm>
#include <cmath>

// Velocities are 8-bit, 0-255: MIDI 2.0's 16 bits narrowed, one bit more than
// MIDI 1.0's 7. Velocity 0 is a real note-on that starts from silence.
namespace velocity {

constexpr int kMax = 255;
constexpr int kMax7 = 127; // a MIDI 1.0 velocity or pressure

// A MIDI 1.0 7-bit value widened to the full range (0 stays 0, 127 becomes 255).
constexpr int from7(int v7) {
  v7 = std::clamp(v7, 0, kMax7);
  return (v7 << 1) | (v7 >> 6);
}

// The note gain, in dB, of a velocity-0 note-on: low enough that the linear gain
// is exactly zero in float. The note starts from true silence.
constexpr float kSilentGainDb = -1000.0f;

constexpr float toFloat(int v) { return static_cast<float>(std::clamp(v, 0, kMax)) / 255.0f; }

// A level in [0,1] to its velocity.
inline int fromFloat(float level) { return static_cast<int>(std::lround(std::clamp(level, 0.0f, 1.0f) * 255.0f)); }

// A velocity narrowed to MIDI 1.0's 7 bits (what an SF2 velocity zone is chosen by).
inline int to7(int v) { return static_cast<int>(std::lround(static_cast<float>(std::clamp(v, 0, kMax)) * 127.0f / 255.0f)); }

// A level in [0,1] to its 7-bit velocity.
inline int floatTo7(float level) { return to7(fromFloat(level)); }

// The release velocity ("lift") scales the release time, fixed when the OFF
// arrives: 128 leaves it alone, 0 makes it four times longer, 255 about four
// times shorter.
inline float releaseTimeFactor(int lift) {
  return std::exp2(static_cast<float>(128 - std::clamp(lift, 0, kMax)) / 64.0f);
}

// The same scaling for an SF2 release generator: (128 - lift) * 18.75
// timecents added, held to the range the SF2 spec allows (-12000 to 8000).
// `seconds` is the generator's release already converted from timecents; a
// zero (a pinned, effectively instant release) stays zero.
constexpr float kSf2ReleaseMinTimecents = -12000.0f;
constexpr float kSf2ReleaseMaxTimecents = 8000.0f;
inline float sf2ReleaseSeconds(float seconds, int lift) {
  lift = std::clamp(lift, 0, kMax);
  if (lift == 128 || !(seconds > 0.0f)) return seconds;
  const float lo = std::exp2(kSf2ReleaseMinTimecents / 1200.0f);
  const float hi = std::exp2(kSf2ReleaseMaxTimecents / 1200.0f);
  const float timecents_offset = static_cast<float>(128 - lift) * 18.75f;
  return std::clamp(seconds * std::exp2(timecents_offset / 1200.0f), lo, hi);
}

} // namespace velocity

#endif
