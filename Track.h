#ifndef _TRACK_H_
#define _TRACK_H_

#include "SampleData.h"
#include "TrackState.h"

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <atomic>

class SongState;
class TrackEventQueue;

namespace tinyxml2 {
  class XMLDocument;
  class XMLElement;
};

class Track {
 public:
  enum Type { MASTER = 1, ROOT, GROUP, INSTRUMENT_TRACK, EFFECT, SAMPLE, SUBSONG, INSTRUMENT };
  Track(Type _type) : id(getNextId()), type(_type) { }
  Track(Type _type, std::string _name) : id(getNextId()), type(_type), name(_name) { }
  Track(int _id, Type _type) : id(_id != -1 ? _id : getNextId()), type(_type) { }
  virtual ~Track() { }
  
  virtual SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) {
    bool child_has_solo = false;
    for (auto & child : getChildren()) {
      if (child->isSolo()) {
	child_has_solo = true;
	break;
      }
    }
    SampleData sd(1, frames, isSolo() || child_has_solo);
     	   
    for (auto & child : getChildren()) {
      auto sd2 = child->render(frames, song_state, instruments, events);
      if (!child->isMuted() && (!child_has_solo || child->isSolo())) {
	sd.mix(sd2, child->getVolume());
      }
    }
    return sd;
  }
  virtual std::unique_ptr<TrackState> createState(unsigned int outSampleRate) const {
    return std::make_unique<TrackState>(outSampleRate);
  }

  virtual tinyxml2::XMLElement * createXML(tinyxml2::XMLDocument & doc) const { return 0; }
  virtual void readXML(tinyxml2::XMLElement & element);
  virtual void populateXML(tinyxml2::XMLElement & element) const;
  virtual std::string getElementName() const { return "track"; }

  virtual std::unique_ptr<TrackState> playNote(float frequency, float velocity, unsigned int outSampleRate, float start_phase = 0.0f) const {
    auto group = createState(outSampleRate);
    for (auto & child : children) {
      auto voice = child->playNote(frequency, velocity, outSampleRate, start_phase);
      if (voice.get()) group->addChild(move(voice));
    }
    return group;
  }

  int getId() const { return id; }
  Type getType() const { return type; }

  float getVolume() const { return volume; }
  void setVolume(float _volume) { volume = _volume; }

  bool isSolo() const { return solo; }
  void setSolo(bool s) { solo = s; }

  bool isMuted() const { return mute; }
  void setMute(bool m) { mute = m; }

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

  void setElevation(float e) { elevation = e; }
  void setAzimuth(float a) { azimuth = a; }
  void setDistance(float d) { distance = d; }
  
  float getElevation() const { return elevation; }
  float getAzimuth() const { return azimuth; }
  float getDistance() const { return distance; }

  bool showVelocityColumn() const { return show_velocity_column; }
  bool showEffectsColumn() const { return show_effects_column; }
  bool showDelayColumn() const { return show_delay_column; }

protected:
  void setId(int _id) { id = _id; }
  
 private:
  int id;
  Type type;
  float volume = 1.00f;
  bool solo = false, mute = false;
  std::string name;
  std::vector<std::unique_ptr<Track> > children;
  float elevation = 0, azimuth = 0, distance = 0;
  
  bool show_velocity_column = true;
  bool show_delay_column = true;
  bool show_aftertouch_column = false;
  bool show_effects_column = true;

  static std::atomic<int> next_id;
};

#endif
