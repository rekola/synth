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
class TrackEventQueue;

class TrackState {
 public:
  explicit TrackState(const ChannelConfiguration & channel_config) : channel_config_(channel_config) { }

  virtual ~TrackState() { }

  virtual TrackInfo getInfo() const { return TrackInfo(true); }

  virtual SampleData render(int frames) {
    SampleData data(getChannelConfiguration(), frames);
    bool is_initialized = false;
    
    for (auto & child : getChildren()) {
      if (child->isPlaying()) {
	if (!is_initialized) {
	  is_initialized = true;
	  data = child->render(frames);
	} else {
	  data.mix(child->render(frames), 1.0f);
	}
      }
    }

    if (!is_initialized) {
      data.zero();
    }
    
    return data;    
  }

  virtual SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) {
    return SampleData();
  }

  virtual void clear() { children_.clear(); }

  void applyAftertouch(float aftertouch) {
    aftertouch_ = aftertouch;
    
    for (auto & child : getChildren()) {
      child->applyAftertouch(aftertouch);
    }
  }

  float getAftertouch() const { return aftertouch_; }
  
  virtual void stopNote() {
    for (auto & child : getChildren()) {
      child->stopNote();
    }
  }

  virtual void killNote() {
    for (auto & child : getChildren()) {
      child->killNote();
    }
  }

  virtual bool isPlaying() const {
    for (auto & child : getChildren()) {
      if (child->isPlaying()) return true;
    }
    return false;
  }
  
  virtual bool isReleased() const {
    for (auto & child : getChildren()) {
      if (!child->isReleased()) return false;
    }
    return true;
  }

  virtual int getVoiceCount() const {
    int n = 0;
    if (isPlaying()) n++;
    for (auto & child : children_) {
      n += child->getVoiceCount();
    }
    return n;
  }
  
  virtual int getAllocatedVoiceCount() const {
    int n = 1;
    for (auto & child : children_) {
      n += child->getAllocatedVoiceCount();
    }
    return n;
  }

  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

  void addChild(std::unique_ptr<TrackState> child) { children_.push_back(std::move(child)); }
  
  const std::vector<std::unique_ptr<TrackState> > & getChildren() const { return children_; }
  std::vector<std::unique_ptr<TrackState> > & getChildren() { return children_; }

protected:
  std::vector<std::unique_ptr<TrackState> > children_;

private:
  ChannelConfiguration channel_config_;
  float aftertouch_ = 1.0f;
};

#endif
