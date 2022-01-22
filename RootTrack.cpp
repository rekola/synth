#include "RootTrack.h"

#include "tinyxml2.h"

using namespace tinyxml2;

void
Track::readXML(XMLElement & element) {
  Track::readXML(element);
  auto azimuth_text = element.Attribute("azimuth");
  auto distance_text = element.Attribute("distance");
  auto elevation_text = element.Attribute("elevation");
  
  setAzimuth(azimuth_text ? strtof(azimuth_text, nullptr) : 0.0f);
  setDistance(distance_text ? strtof(distance_text, nullptr) : 0.0f);
  setElevation(elevation_text ? strtof(elevation_text, nullptr) : 0.0f);  
}

void
Track::populateXML(tinyxml2::XMLElement & element) const {
  Track::populateXML(element);
  
  element.SetAttribute("azimuth", getAzimuth());
  element.SetAttribute("distance", getDistance());
  element.SetAttribute("elevation", getElevation());

}
