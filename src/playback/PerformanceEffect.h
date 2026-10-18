#ifndef _PERFORMANCEEFFECT_H_
#define _PERFORMANCEEFFECT_H_

#include <algorithm>

// A performance effect: held on a track from the Launchpad's Custom grid, it
// changes how the track sounds until released and is never written to the
// song. `pressure` is 0 for a plain press and up to 127 with pad pressure.
enum class PerformanceEffect { NONE,
                               DUCK,
                               THROW_A,
                               THROW_B,
                               ORBIT,
                               STUTTER,
                               ERODE,
                               SHUFFLE,
                               SHATTER,
                               GATE,
                               CRUSH };

namespace performance {

// The depth a release is sent with.
constexpr int kRelease = -1;

// The track id an effect held on the master - the whole mix - is sent with.
constexpr int kMasterTarget = -2;

// How long a depth change takes to reach the voices, so no press clicks.
constexpr float kRampSeconds = 0.01f;
// How long an orbit takes to turn back to where the song has the track.
constexpr float kOrbitReturnSeconds = 0.3f;

// The Custom grid's rows, bottom (index 0) to top. NONE rows are not built
// yet and stay dark.
constexpr PerformanceEffect kGridRows[8] = {
    PerformanceEffect::DUCK,
    PerformanceEffect::NONE, // filter
    PerformanceEffect::CRUSH,
    PerformanceEffect::GATE,
    PerformanceEffect::SHATTER,
    PerformanceEffect::ERODE,
    PerformanceEffect::SHUFFLE,
    PerformanceEffect::STUTTER,
};

inline PerformanceEffect effectForGridRow(int y) {
  return y >= 0 && y < 8 ? kGridRows[y] : PerformanceEffect::NONE;
}

// Stutter: the slice of the track's output it repeats, in rows - a quarter
// note on a press, then an eighth, a sixteenth and a 32nd pressing harder.
inline float stutterSliceRows(int pressure) {
  return pressure < 32 ? 4.0f : pressure < 64 ? 2.0f
                            : pressure < 96   ? 1.0f
                                              : 0.5f;
}
// The fade at the stutter's seams.
constexpr float kStutterFadeSeconds = 0.003f;

// Shuffle: the slices it plays in a new order, in rows - an eighth note on
// a press, then a sixteenth, a 32nd and a 64th pressing harder.
inline float shuffleSliceRows(int pressure) {
  return pressure < 32 ? 2.0f : pressure < 64 ? 1.0f
                            : pressure < 96   ? 0.5f
                                              : 0.25f;
}
// The most a shuffle records, in rows.
constexpr int kShuffleMaxRows = 32;

// Gate: its period in rows (open for the first half) - an eighth note on a
// press, a sixteenth and a 32nd pressing harder.
inline float gatePeriodRows(int pressure) {
  return pressure < 43 ? 2.0f : pressure < 86 ? 1.0f
                                              : 0.5f;
}
// The gate's edges.
constexpr float kGateEdgeSeconds = 0.002f;

// Erode, Shatter and Crush: how deep (0-1). Erode thins its grains out,
// Shatter grows denser and wider, Crush loses rate and bits.
inline float depth(int pressure) {
  return static_cast<float>(std::clamp(pressure, 0, 127)) / 127.0f;
}

// Duck: the gain on the track's dry signal, -12 dB on a press, down to
// silence at full pressure.
inline float duckGain(int pressure) {
  return 0.25f * (1.0f - static_cast<float>(std::clamp(pressure, 0, 127)) / 127.0f);
}

// Orbit: turns around the listener per bar, one on a press, up to four.
inline float orbitTurnsPerBar(int pressure) {
  return 1.0f + 3.0f * static_cast<float>(std::clamp(pressure, 0, 127)) / 127.0f;
}

} // namespace performance

#endif
