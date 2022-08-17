#ifndef _TRACK_H_
#define _TRACK_H_

#include "SongObject.h"

#include "SampleData.h"
#include "TrackState.h"
#include "TrackType.h"

#include <string>
#include <vector>
#include <memory>
#include <map>

class SongState;
class TrackEventQueue;

class Track : public SongObject {
 public:
  Track(TrackType _type) : type(_type) { }
  Track(TrackType _type, std::string _name) : SongObject(_name), type(_type) { }
  Track(int _id, TrackType _type) : SongObject(_id), type(_type) { }
  
  virtual SampleData render(int frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events);

  virtual std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const {
    return std::make_unique<TrackState>(config);
  }

  void loadParameters(const ParameterSource & input) override {
    SongObject::loadParameters(input);
    setSolo(input.getBool("solo"));
    setMute(input.getBool("mute"));
  }

  void storeParameters(ParameterSource & output) const override {
    SongObject::storeParameters(output);
    if (isSolo()) output.set("solo", true);
    if (isMuted()) output.set("mute", true);
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

  bool isSolo() const { return solo; }
  void setSolo(bool s) { solo = s; }

  bool isMuted() const { return mute; }
  void setMute(bool m) { mute = m; }

  const Track & getChild(int i) const { return *(children[i]); }
  Track & getChild(int i) { return *(children[i]); }
  
  Track & addChild(std::unique_ptr<Track> track) { children.push_back(std::move(track)); return *(children.back()); }

  std::vector<std::unique_ptr<Track> > & getChildren() { return children; }
  const std::vector<std::unique_ptr<Track> > & getChildren() const { return children; }

  bool showNoteColumn() const { return show_note_column; }
  bool showVelocityColumn() const { return show_velocity_column; }
  bool showEffectsColumn() const { return show_effects_column; }
  bool showDelayColumn() const { return show_delay_column; }
  
 private:
  TrackType type;
  bool solo = false, mute = false;
  std::vector<std::unique_ptr<Track> > children;

  bool show_note_column = true;
  bool show_velocity_column = true;
  bool show_delay_column = true;
  bool show_effects_column = true;
};

#endif
