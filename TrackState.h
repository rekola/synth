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
  explicit TrackState(const ChannelConfiguration & channel_config) : channel_config_(channel_config) { }

  virtual ~TrackState() { }

  virtual TrackInfo getInfo() const { return TrackInfo(true); }

  virtual SampleData render(int frames) {
    SampleData data(getChannelConfiguration(), frames);
    data.zero();

    for (auto & child : getChildren()) {
      if (child->isPlaying()) {
	data.mix(child->render(frames), 1.0f);
      }
    }
    
    return data;    
  }

  virtual SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) {
    return SampleData();
  }

  virtual void clear() { children_.clear(); }

  virtual void playNote(float frequency, float velocity) {
    for (auto & child : getChildren()) {
      child->playNote(frequency, velocity);
    }
  }

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

  void applyAftertouch(float aftertouch) {
    aftertouch_ = aftertouch;
    
    for (auto & child : getChildren()) {
      child->applyAftertouch(aftertouch);
    }
  }

  float getAftertouch() const { return aftertouch_; }

  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

  void addChild(std::unique_ptr<TrackState> child) { children_.push_back(std::move(child)); }
  
  const std::vector<std::unique_ptr<TrackState> > & getChildren() const { return children_; }
  std::vector<std::unique_ptr<TrackState> > & getChildren() { return children_; }

protected:
  static inline float getRandF() {
    return (float)rand() / RAND_MAX;
  }
  
private:
  ChannelConfiguration channel_config_;
  std::vector<std::unique_ptr<TrackState> > children_;
  float aftertouch_ = 1.0f;
};

#endif
