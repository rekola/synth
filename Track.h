#ifndef _TRACK_H_
#define _TRACK_H_

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <atomic>

class SongState;
class Instrument;
class SampleData;
class TrackEventQueue;

namespace tinyxml2 {
  class XMLElement;
};

class Track {
 public:
  enum Type { MASTER = 1, GROUP, INSTRUMENT, EFFECT, SAMPLE, SUBSONG };
  Track(Type _type) : id(getNextId()), type(_type) { }
  Track(int _id, Type _type) : id(_id != -1 ? _id : getNextId()), type(_type) { }
  virtual ~Track() { }
  
  virtual SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) = 0;
  virtual void readXML(tinyxml2::XMLElement & element);
  virtual void populateXML(tinyxml2::XMLElement & element) const;

  int getId() const { return id; }
  Type getType() const { return type; }

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

  const Track * getChildById(int _id) const {
    if (id == _id) return this;
    for (auto & child : children) {
      auto r = child->getChildById(_id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getChildById(int _id) {
    if (id == _id) return this;
    for (auto & child : children) {
      auto r = child->getChildById(_id);
      if (r) return r;
    }
    return nullptr;
  }

  static int getNextId() {
    return next_id.fetch_add(1);
  }

protected:
  void setId(int _id) { id = _id; }
  
 private:
  int id;
  Type type;
  float volume = 1.00f;
  bool solo = false, mute = false;
  float elevation = 0, azimuth = 0, distance = 0;
  std::string name;
  std::vector<std::unique_ptr<Track> > children;
  
  static std::atomic<int> next_id;
};

#endif
