#ifndef _SENDFADER_H_
#define _SENDFADER_H_

#include <algorithm>

// The range a send is shown over by a control surface: the Launchpad's fader
// rows and the clip grid's dials. Unity (0 dB) is the top, kSendFloorDb the
// lowest step, and anything below is off.
constexpr float kSendFloorDb = -36.0f;

// A send level in dB as a 0..1 position over that range (0: the floor or
// less, 1: unity or more).
inline float sendDbToFraction(float db) { return std::clamp((db - kSendFloorDb) / -kSendFloorDb, 0.0f, 1.0f); }

#endif
