#include "Track.h"

#include "tinyxml2.h"

using namespace tinyxml2;

std::atomic<int> Track::next_id(1000);

void
Track::readXML(XMLElement & element) {
  auto id_text = element.Attribute("id");
  auto name_text = element.Attribute("name");
  auto azimuth_text = element.Attribute("azimuth");
  auto distance_text = element.Attribute("distance");
  auto elevation_text = element.Attribute("elevation");
  auto volume_text = element.Attribute("volume");
  auto solo_text = element.Attribute("solo");
  auto mute_text = element.Attribute("mute");

  setId(id_text ? atoi(id_text) : -1);
  setName(name_text ? name_text : "");
  setAzimuth(azimuth_text ? atof(azimuth_text) : 0.0f);
  setDistance(distance_text ? atof(distance_text) : 0.0f);
  setElevation(elevation_text ? atof(elevation_text) : 0.0f);
  setVolume(volume_text ? atof(volume_text) : 1.0f);
  setSolo(solo_text && atoi(solo_text) ? true : false);
  setMute(mute_text && atoi(mute_text) ? true : false);
}

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
