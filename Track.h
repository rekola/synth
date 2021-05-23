#ifndef _TRACK_H_
#define _TRACK_H_

#include <string>
#include <vector>
#include <memory>
#include <map>

class TrackState;
class TrackEvent;
class Instrument;
class SampleData;

namespace tinyxml2 {
  class XMLElement;
};

class Track {
 public:
  enum Type { MASTER = 1, GROUP, INSTRUMENT, EFFECT, SAMPLE, SUBSONG };
  Track(Type _type) : type(_type) { }
  virtual ~Track() { }
  
  virtual SampleData render(size_t frames, TrackState & state, const std::vector<std::unique_ptr<Instrument> > & instruments, std::map<unsigned int, std::vector<TrackEvent> > & pending_events) = 0;
  virtual void populateXML(tinyxml2::XMLElement & element) const;
    
  Type getType() { return type; }

  float getVolume() const { return volume; }
  void setVolume(float _volume) { volume = _volume; }

  bool isSolo() const { return solo; }
  void setSolo(bool s) { solo = s; }

  bool isMuted() const { return mute; }
  void setMute(bool m) { mute = m; }

  void setElevation(float e) { elevation = e; }
  void setAzimuth(float a) { azimuth = a; }
  void setDistance(float d) { distance = d; }

  float getElevation() const { return elevation; }
  float getAzimuth() const { return azimuth; }
  float getDistance() const { return distance; }  

  void setName(std::string _name) { name = _name; }
  const std::string & getName() const { return name; }

  const Track & getChild(size_t i) const { return *(children[i]); }
  Track & getChild(size_t i) { return *(children[i]); }
  Track & addChild(std::unique_ptr<Track> track) { children.push_back(std::move(track)); return *(children.back()); }

  std::vector<std::unique_ptr<Track> > & getChildren() { return children; }
  const std::vector<std::unique_ptr<Track> > & getChildren() const { return children; }

 private:
  Type type;
  float volume = 1.00f;
  bool solo = false, mute = false;
  float elevation = 0, azimuth = 0, distance = 0;
  std::string name;
  std::vector<std::unique_ptr<Track> > children;
};

#endif
