#ifndef _SENDFADER_H_
#define _SENDFADER_H_

#include <algorithm>
#include <cmath>

// The levels a send is set to from a control surface: the Launchpad's fader
// rows, a recorded glide command's target nibble and the clip grid's dials all
// use these 16 positions. Position 0 is off, positions 1..15 are 3 dB apart up
// to unity (0 dB) at 15, so the lowest real level is -42 dB.
constexpr int kSendPositions = 16;
constexpr int kSendTopPosition = kSendPositions - 1;
constexpr float kSendStepDb = 3.0f;
constexpr float kSendOffDb = -100.0f;
// Where position 0 sits on a dial's arc, one step below the lowest level.
constexpr float kSendArcBottomDb = -kSendStepDb * static_cast<float>(kSendTopPosition);

inline float sendPositionToDb(int position) {
  if (position <= 0) return kSendOffDb;
  return kSendArcBottomDb + kSendStepDb * static_cast<float>(std::min(position, kSendTopPosition));
}

// The nearest position to a level; anything nearer to the arc's bottom than to
// the lowest level is off, and anything above unity is the top.
inline int sendDbToPosition(float db) {
  float position = std::round((db - kSendArcBottomDb) / kSendStepDb);
  return static_cast<int>(std::clamp(position, 0.0f, static_cast<float>(kSendTopPosition)));
}

// A send level as a 0..1 position over the arc (0: off or the bottom, 1: unity or more).
inline float sendDbToFraction(float db) { return std::clamp((db - kSendArcBottomDb) / -kSendArcBottomDb, 0.0f, 1.0f); }

#endif
