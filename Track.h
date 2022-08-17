#ifndef _TRACK_H_
#define _TRACK_H_

#include "SongObject.h"

#include "SampleData.h"
#include "TrackState.h"
#include "ParameterSource.h"
#include "TrackType.h"

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <atomic>

class SongState;
class TrackEventQueue;

class Track : public SongObject {
 public:
  Track(TrackType _type) : type(_type) { }
  Track(TrackType _type, std::string _name) : type(_type), name(_name) { }
  Track(int _id, TrackType _type) : SongObject(_id), type(_type) { }
  
  virtual SampleData render(int frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events);

  virtual std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const {
    return std::make_unique<TrackState>(config);
  }

  virtual void loadParameters(const ParameterSource & input) {
    setId(input.getInt("id", -1));
    setName(input.getText("name"));
    setVolume(input.getFloat("volume", 1.0f));
    setSolo(input.getBool("solo"));
    setMute(input.getBool("mute"));
  }

  virtual void storeParameters(ParameterSource & output) const {
    if (!getName().empty()) output.set("name", getName());
    if (isSolo()) output.set("solo", true);
    if (isMuted()) output.set("mute", true);
    output.set("volume", getVolume());
  }
  
  virtual std::string getElementName() const = 0;

  virtual std::unique_ptr<TrackState> playNote(const ChannelConfiguration & config, float azimuth, float frequency, float velocity, float start_phase = 0.0f) const {
    auto group = createState(config);
    for (auto & child : children) {
      auto voice = child->playNote(config, azimuth, frequency, velocity, start_phase);
      if (voice.get()) group->addChild(std::move(voice));
    }
    return group;
  }

  TrackType getType() const { return type; }

  float getVolume() const { return volume; }
  void setVolume(float _volume) { volume = _volume; }

  bool isSolo() const { return solo; }
  void setSolo(bool s) { solo = s; }

  bool isMuted() const { return mute; }
  void setMute(bool m) { mute = m; }

  void setName(std::string _name) { name = _name; }
  const std::string & getName() const { return name; }

  const Track & getChild(int i) const { return *(children[i]); }
  Track & getChild(int i) { return *(children[i]); }
  
  Track & addChild(std::unique_ptr<Track> track) { children.push_back(std::move(track)); return *(children.back()); }

  std::vector<std::unique_ptr<Track> > & getChildren() { return children; }
  const std::vector<std::unique_ptr<Track> > & getChildren() const { return children; }

  const Track * getChildById(int id) const {
    if (id == getId()) return this;
    for (auto & child : children) {
      auto r = child->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getChildById(int id) {
    if (id == getId()) return this;
    for (auto & child : children) {
      auto r = child->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  bool showNoteColumn() const { return show_note_column; }
  bool showVelocityColumn() const { return show_velocity_column; }
  bool showEffectsColumn() const { return show_effects_column; }
  bool showDelayColumn() const { return show_delay_column; }
  
 private:
  TrackType type;
  float volume = 1.00f;
  bool solo = false, mute = false;
  std::string name;
  std::vector<std::unique_ptr<Track> > children;

  bool show_note_column = true;
  bool show_velocity_column = true;
  bool show_delay_column = true;
  bool show_effects_column = true;
};

#endif
