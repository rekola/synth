#ifndef _TRACK_H_
#define _TRACK_H_

#include "StatefulSongObject.h"

#include "SampleData.h"
#include "TrackType.h"
#include "RenderContext.h"

#include <string>
#include <vector>
#include <memory>
#include <map>

class SongState;
class TrackEventQueue;

class Track : public StatefulSongObject {
 public:
  Track(TrackType _type) : type(_type) { }
  
  virtual SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) const {
    bool child_has_solo = false;
    for (auto & child : getChildren()) {
      if (child->isSolo()) {
	child_has_solo = true;
	break;
      }
    }
    
    SampleData sd(context.getChannelConfiguration(), frames, isSolo() || child_has_solo);
    sd.zero();
    
    for (auto & child : getChildren()) {
      auto sd2 = child->render(frames, instruments, context);
      if (!child->isMuted() && (!child_has_solo || child->isSolo())) {
	sd.mix(sd2, child->getVolume());
      }
    }
    return sd;
  }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override {
    return std::make_unique<TrackState>(config);
  }

  std::unique_ptr<TrackState> createStateTree(const ChannelConfiguration & config) {
    auto state = createState(config);
    for (auto & child : getChildren()) {
      state->addChild(child->createStateTree(config));
    }
    return state;
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

  int getDepth() const {
    int max_depth = 0;
    for (auto & child : getChildren()) {
      auto d = child->getDepth();
      if (d > max_depth) max_depth = d;
    }
    return 1 + max_depth;
  }

  const Track * getChildByInternalId(int id) const {
    if (getInternalId() == id) return this;
    for (auto & child : children) {
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getChildByInternalId(int id) {
    if (getInternalId() == id) return this;
    for (auto & child : children) {
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  const Track * getChildById(std::string_view id) const {
    if (getId() == id) return this;
    for (auto & child : children) {
      auto r = child->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getChildById(std::string_view id) {
    if (getId() == id) return this;
    for (auto & child : children) {
      auto r = child->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

 private:
  TrackType type;
  bool solo = false, mute = false;
  std::vector<std::unique_ptr<Track> > children;
};

#endif
