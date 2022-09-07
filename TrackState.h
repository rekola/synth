#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "State.h"
#include "TrackInfo.h"
#include "SampleData.h"
#include "ChannelConfiguration.h"

#include <vector>
#include <memory>
#include <algorithm>

class Track;
class RenderContext;

class TrackState {
 public:
  explicit TrackState(const ChannelConfiguration & channel_config)
    : channel_config_(channel_config), solo_(false), muted_(false) { }

  explicit TrackState(const ChannelConfiguration & channel_config, bool solo, bool muted)
    : channel_config_(channel_config), solo_(solo), muted_(muted) { }

  virtual ~TrackState() { }
  
  // For rendering voices
  virtual SampleData render(int frames) {
    SampleData data(getChannelConfiguration(), frames);
    data.zero();

    bool is_active = false;
    for (auto & [ id, child ] : getChildren()) {
      if (child->isPlaying()) {
	data.mix(child->render(frames), child->getVolume());
	is_active = true;
      }
    }

    applyEffect(data);
    setTrackInfo(TrackInfo( is_active, data.isClipping() ));
   
    return data;    
  }

  // For rendering tracks
  virtual SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) {
    bool child_has_solo = false;
    for (auto & [ id, child ] : getChildren()) {
      if (child->isSolo()) {
	child_has_solo = true;
	break;
      }
    }
    
    SampleData sd(getChannelConfiguration(), frames, isSolo() || child_has_solo);
    sd.zero();

    bool is_active = false;
    for (auto & [ id, child ] : getChildren()) {
      auto sd2 = child->render(frames, instruments, context);
      if (!child->isMuted() && (!child_has_solo || child->isSolo())) {
	sd.mix(sd2, child->getVolume());
	is_active = true;
      }
    }

    applyEffect(sd);
    setTrackInfo(TrackInfo( is_active, sd.isClipping() ));

    return sd;
  }

  virtual void clear() { children_.clear(); }

  virtual void playNote(float frequency, float velocity) {
    for (auto & [ id, child ] : getChildren()) {
      child->playNote(frequency, velocity);
    }
  }

  virtual void stopNote() {
    for (auto & [ id, child ] : getChildren()) {
      child->stopNote();
    }
  }
  
  virtual void killNote() {
    for (auto & [ id, child ] : getChildren()) {
      child->killNote();
    }
  }

  virtual bool isPlaying() const {
    for (auto & [ id, child ] : getChildren()) {
      if (child->isPlaying()) return true;
    }
    return false;
  }
  
  virtual int getVoiceCount() const {
    int n = 0;
    if (isPlaying()) n++;
    for (auto & [ id, child ] : getChildren()) {
      n += child->getVoiceCount();
    }
    return n;
  }
  
  virtual int getAllocatedVoiceCount() const {
    int n = 1;
    for (auto & [ id, child ] : getChildren()) {
      n += child->getAllocatedVoiceCount();
    }
    return n;
  }

  void applyAftertouch(float aftertouch) {
    aftertouch_ = aftertouch;
    
    for (auto & [ id, child ] : getChildren()) {
      child->applyAftertouch(aftertouch);
    }
  }

  float getAftertouch() const { return aftertouch_; }

  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

  void addChild(int internal_id, std::unique_ptr<TrackState> child) { children_[internal_id] = std::move(child); }

  TrackState * getChildByInternalId(int id) {
    for (auto & [ child_id, child ] : getChildren()) {
      if (id == child_id) return child.get();
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  const TrackState * getChildByInternalId(int id) const {
    for (auto & [ child_id, child ] : getChildren()) {
      if (id == child_id) return child.get();
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  void removeChild(int id) {
    auto it = children_.find(id);
    if (it != children_.end()) children_.erase(it);
    else {
      for (auto & [ child_id, child ] : getChildren()) child->removeChild(id);
    }
  }

  void getAllTrackInfo(std::unordered_map<int, TrackInfo> & info) const {
    for (auto & [ id, child ] : getChildren()) {
      info[id] = child->getTrackInfo();
      child->getAllTrackInfo(info);
    }
  }
  
  const std::unordered_map<int, std::unique_ptr<TrackState> > & getChildren() const { return children_; }
  std::unordered_map<int, std::unique_ptr<TrackState> > & getChildren() { return children_; }

  bool isSolo() const { return solo_; }
  bool isMuted() const { return muted_; }
  float getVolume() const { return 1.0f; }

  TrackState & getChildState(const Track & track);
  
protected:
  virtual void applyEffect(SampleData & input) { }

  const TrackInfo & getTrackInfo() const { return track_info_; }
  void setTrackInfo(TrackInfo track_info) { track_info_ = std::move(track_info); }
  
  static inline float getRandF() {
    return (float)rand() / RAND_MAX;
  }
  
private:
  int internal_id_;
  ChannelConfiguration channel_config_;
  std::unordered_map<int, std::unique_ptr<TrackState> > children_;
  bool solo_, muted_;
  float aftertouch_ = 1.0f;
  TrackInfo track_info_;
};

#endif
