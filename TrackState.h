#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "State.h"
#include "TrackInfo.h"
#include "SampleData.h"
#include "ChannelConfiguration.h"

#include <vector>
#include <memory>

class TrackState : public State {
 public:
  TrackState(ChannelConfiguration _channel_config, unsigned int _samplerate) : State(_samplerate), channel_config(_channel_config) { }

  virtual void apply(SampleData & input_data) { }
  virtual TrackInfo getInfo() const { return TrackInfo(true); }

  virtual SampleData render(size_t frames) {
    if (getChildren().empty()) {
      return SampleData(getChannelConfiguration() == ChannelConfiguration::MONO ? 1 : 2, frames);
    } else {
      auto it = getChildren().begin();
      auto data = (*it)->render(frames);
      for (it++; it != getChildren().end(); it++) {
	data.mix((*it)->render(frames), (size_t)0);
      }
      apply(data);
      return data;
    }
  }

  virtual void applyAftertouch(float aftertouch) {
    for (auto & child : getChildren()) {
      child->applyAftertouch(aftertouch);
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
  
  virtual bool isReleased() const {
    for (auto & child : getChildren()) {
      if (!child->isReleased()) return false;
    }
    return true;
  }    

  ChannelConfiguration getChannelConfiguration() const { return channel_config; }

  void addChild(std::unique_ptr<TrackState> child) { children.push_back(std::move(child)); }
  const std::vector<std::unique_ptr<TrackState> > & getChildren() const { return children; }
  std::vector<std::unique_ptr<TrackState> > & getChildren() { return children; }

private:
  ChannelConfiguration channel_config;
  std::vector<std::unique_ptr<TrackState> > children;
};

#endif
