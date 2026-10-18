#ifndef _SONGSCHEMA_H_
#define _SONGSCHEMA_H_

#include "../doc/Schema.h"
#include "../util/constants.h"

// What the song's document nodes carry. A property is declared here once; the
// types a Song accessor uses are cast from these.
namespace songschema {

// type "song" - the root node.
inline const doc::Prop<int> kTuning{"tuning", 3};  // Tuning::EDO31
inline const doc::Prop<int> kKey{"key", 0};
inline const doc::Prop<int> kScale{"scale", 0};  // Scale::NONE
inline const doc::Prop<int> kTempo{"tempo", 140};
inline const doc::Prop<int> kTimeNumerator{"timeNumerator", 4};
inline const doc::Prop<int> kTimeDenominator{"timeDenominator", 4};
inline const doc::Prop<int> kSwing{"swing", 50};  // swing::kStraight
inline const doc::Prop<bool> kRecordQuantize{"recordQuantize", false};
inline const doc::Prop<float> kEarHeight{"earHeight", constants::DEFAULT_EAR_HEIGHT};
inline const doc::Prop<bool> kFloorReflection{"floorReflection", constants::DEFAULT_FLOOR_REFLECTION_ENABLED};
inline const doc::Prop<float> kFloorReflectionStrength{"floorReflectionStrength", constants::DEFAULT_FLOOR_REFLECTION_STRENGTH};
inline const doc::Prop<float> kGroundAbsorption{"groundAbsorption", constants::DEFAULT_GROUND_ABSORPTION};
// The signature a launched scene set (RunningBars): numerator 0 = none.
inline const doc::Prop<int> kTransportNumerator{"transportNumerator", 0};
inline const doc::Prop<int> kTransportDenominator{"transportDenominator", 0};
inline const doc::Prop<int> kTransportOrigin{"transportOrigin", 0};
inline constexpr const char * kScenesSlot = "scenes";
inline constexpr const char * kLocatorsSlot = "locators";

// type "scene", by position in the root's scenes slot.
inline const doc::Prop<std::string> kSceneName{"name", ""};
inline const doc::Prop<int> kSceneTempo{"tempo", 0};
inline const doc::Prop<int> kSceneTimeNumerator{"timeNumerator", 0};
inline const doc::Prop<int> kSceneTimeDenominator{"timeDenominator", 0};

// type "locator", sorted by row in the root's locators slot.
inline const doc::Prop<int> kLocatorRow{"row", 0};
inline const doc::Prop<std::string> kLocatorText{"text", ""};

}  // namespace songschema

#endif
