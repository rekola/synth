#ifndef _SENDFADER_H_
#define _SENDFADER_H_

#include <algorithm>

// The range a send is shown over by a control surface: the Launchpad's fader
// rows and the clip grid's dials. Unity (0 dB) is the top, kSendFloorDb the
// lowest step, and anything below is off. The steps are 6 dB apart, so a
// dial's arc starts one step below the floor (kSendArcBottomDb), where off
// is drawn, and the two lowest fader rows point in different directions.
constexpr float kSendFloorDb = -36.0f;
constexpr float kSendArcBottomDb = kSendFloorDb - 6.0f;

// A send level in dB as a 0..1 position over the arc (0: off or the bottom, 1:
// unity or more).
inline float sendDbToFraction(float db) { return std::clamp((db - kSendArcBottomDb) / -kSendArcBottomDb, 0.0f, 1.0f); }

#endif
