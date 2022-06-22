#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "State.h"
#include "TrackInfo.h"
#include "SampleData.h"
#include "ChannelConfiguration.h"

#include <vector>
#include <memory>
#include <algorithm>

class TrackState {
 public:
  explicit TrackState(const ChannelConfiguration & channel_config) : channel_config_(channel_config) { }
  virtual ~TrackState() { }

  virtual TrackInfo getInfo() const { return TrackInfo(true); }

  virtual SampleData render(int frames) {    
    SampleData data(getChannelConfiguration(), frames);
    data.zero();
    
    for (auto & [ id, child ] : getChildren()) {
      if (child->isPlaying()) {
	data.mix(child->render(frames), 1.0f);
      }
    }
    return data;
  }
  
  void clear() { children_.clear(); }

  virtual void applyAftertouch(float aftertouch) {
    for (auto & [ id, child ] : getChildren()) {
      child->applyAftertouch(aftertouch);
    }
  }

  void applyAftertouch(int column, float aftertouch) {
    for (auto & [ id, voice ] : children_) {
      if (column == id && voice->isPlaying()) {
	voice->applyAftertouch(aftertouch);
      }
    }
  }
  
  virtual void stopNote() {
    for (auto & [ id, child ] : getChildren()) {
      child->stopNote();
    }
  }

  void stopVoices(int column) {
    for (auto & [id, child] : children_) {
      if (column == id && child->isPlaying()) {
	child->stopNote();
      }
    }
  }

  virtual void killNote() {
    for (auto & [ id, child ] : getChildren()) {
      child->killNote();
    }
  }

  virtual bool isPlaying() const {
    for (auto & [ id, child ] : getChildren()) {
      if (!child->isPlaying()) return false;
    }
    return true;
  }
  
  virtual bool isReleased() const {
    for (auto & [ id, child ] : getChildren()) {
      if (!child->isReleased()) return false;
    }
    return true;
  }

  static inline bool is_not_playing(const std::pair<int, std::unique_ptr<TrackState> > & a) { return a.first >= 0 && !a.second->isPlaying(); }

  int getVoiceCount() const {
    int n = 0;
    if (isPlaying()) n++;
    for (auto & [ id, voice ] : children_) {
      n += voice->getVoiceCount();
    }
    return n;
  }
  
  int getAllocatedVoiceCount() const {
    int n = 1;
    for (auto & [ id, voice ] : children_) {
      n += voice->getAllocatedVoiceCount();
    }
    return n;
  }

  TrackState & addVoice(int identifier, std::unique_ptr<TrackState> voice) {
    children_.erase(std::remove_if(children_.begin(), children_.end(), is_not_playing), children_.end());
    
    children_.push_back(std::pair(identifier, std::move(voice)));
    return *(children_.back().second);
  }

  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

  void addChild(int column, std::unique_ptr<TrackState> child) { children_.push_back(std::pair(column, std::move(child))); }
  void addChild(std::unique_ptr<TrackState> child) { addChild(-1, std::move(child)); }
  
  const std::vector<std::pair<int, std::unique_ptr<TrackState> > > & getChildren() const { return children_; }
  std::vector<std::pair<int, std::unique_ptr<TrackState> > > & getChildren() { return children_; }

private:
  ChannelConfiguration channel_config_;
  std::vector<std::pair<int, std::unique_ptr<TrackState> > > children_;
};

#endif
