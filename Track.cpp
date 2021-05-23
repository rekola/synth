#include "Track.h"

#include "tinyxml2.h"

using namespace tinyxml2;

void
Track::populateXML(tinyxml2::XMLElement & element) const {
  if (!getName().empty()) element.SetAttribute("name", getName().c_str());
  if (isSolo()) element.SetAttribute("solo", "1");
  if (isMuted()) element.SetAttribute("mute", "1");
  element.SetAttribute("azimuth", getAzimuth());
  element.SetAttribute("distance", getDistance());
  element.SetAttribute("elevation", getElevation());
  element.SetAttribute("volume", getVolume());
}
