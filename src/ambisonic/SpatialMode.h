#ifndef _SPATIALMODE_H_
#define _SPATIALMODE_H_

#include <string>

// How a leaf track places each of its notes around the track's position
// (LeafTrack::getSpatialMode()). AUTO leaves the choice to the instrument.
enum class SpatialMode { AUTO,
                         RING,
                         ARC };

inline const char * spatialModeName(SpatialMode mode) {
  switch (mode) {
    case SpatialMode::RING:
      return "ring";
    case SpatialMode::ARC:
      return "arc";
    default:
      return "auto";
  }
}

inline SpatialMode spatialModeFromName(const std::string & name) {
  if (name == "ring") return SpatialMode::RING;
  if (name == "arc") return SpatialMode::ARC;
  return SpatialMode::AUTO;
}

#endif
