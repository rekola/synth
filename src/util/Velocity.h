#ifndef _VELOCITY_H_
#define _VELOCITY_H_

#include <algorithm>
#include <cmath>
#include <cstdint>

// Velocities are stored at 16 bits (the MIDI 2.0 width) and shown and edited
// at 7 bits (the MIDI 1.0 width). The two scales meet at the centre: 64 is
// exactly 0x8000, and 127 is exactly 0xFFFF.
namespace velocity {

constexpr int kMax7 = 127;
constexpr uint16_t kMax16 = 0xFFFF;

// The smallest level a note-on sounds at: a velocity 0 note-on is a real
// note, at this level, so later rows can swell it.
constexpr float kMinSoundingLevel = 1.0f / 127.0f;

// 7 to 16 bits by min-center-max upscaling: the bits are shifted up and, above
// the centre, the low bits repeat the value's own so 127 reaches 0xFFFF.
constexpr uint16_t from7(int v7) {
  const unsigned v = static_cast<unsigned>(std::clamp(v7, 0, kMax7));
  unsigned out = v << 9;
  if (v <= 64) return static_cast<uint16_t>(out);
  unsigned repeat = (v & 63u) << 3;
  while (repeat != 0) {
    out |= repeat;
    repeat >>= 6;
  }
  return static_cast<uint16_t>(out);
}

// 16 to 7 bits: the top seven bits, so to7(from7(v)) == v for every v.
constexpr int to7(uint16_t v16) { return v16 >> 9; }

constexpr float toFloat(uint16_t v16) { return static_cast<float>(v16) / 65535.0f; }

// A level in [0,1] back to the 16-bit scale.
inline uint16_t fromFloat(float level) {
  return static_cast<uint16_t>(std::lround(std::clamp(level, 0.0f, 1.0f) * 65535.0f));
}
// A level in [0,1] to its 7-bit velocity (what a velocity zone is chosen by).
inline int floatTo7(float level) { return to7(fromFloat(level)); }

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
