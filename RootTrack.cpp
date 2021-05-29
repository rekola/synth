#include "RootTrack.h"

#include "tinyxml2.h"

using namespace tinyxml2;

void
Track::readXML(XMLElement & element) {
  Track::readXML(element);
  auto azimuth_text = element.Attribute("azimuth");
  auto distance_text = element.Attribute("distance");
  auto elevation_text = element.Attribute("elevation");
  
  setAzimuth(azimuth_text ? atof(azimuth_text) : 0.0f);
  setDistance(distance_text ? atof(distance_text) : 0.0f);
  setElevation(elevation_text ? atof(elevation_text) : 0.0f);  
}

void
Track::populateXML(tinyxml2::XMLElement & element) const {
  Track::populateXML(element);
  
  element.SetAttribute("azimuth", getAzimuth());
  element.SetAttribute("distance", getDistance());
  element.SetAttribute("elevation", getElevation());

}
