#ifndef _ROOTTRACK_H_
#define _ROOTTRACK_H_

#include "Track.h"

class RootTrack : public Track {
public:
  RootTrack() : Track(ROOT) { }
  RootTrack(int _id, float _azimuth) : Track(_id, ROOT), azimuth(_azimuth) { }
  
  virtual std::string getElementName() const override { return "root"; }

  void setElevation(float e) { elevation = e; }
  void setAzimuth(float a) { azimuth = a; }
  void setDistance(float d) { distance = d; }
  
  float getElevation() const { return elevation; }
  float getAzimuth() const { return azimuth; }
  float getDistance() const { return distance; }  

private:
  float elevation = 0, azimuth = 0, distance = 0;
};

#endif
