#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "State.h"
#include "TrackInfo.h"
#include "SampleData.h"
#include "ChannelConfiguration.h"

#include <vector>
#include <memory>
#include <algorithm>

class TrackState : public State {
 public:
  explicit TrackState(ChannelConfiguration channel_config, int outSampleRate) : State(outSampleRate), channel_config_(channel_config) { }

  virtual void apply(SampleData & input_data) { }
  virtual TrackInfo getInfo() const { return TrackInfo(true); }

  virtual SampleData render(size_t frames) {
    if (getChildren().empty()) {
      return SampleData(getChannelConfiguration(), frames);
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
  
  void clearVoices() { voices_.clear(); }

  void render(SampleData & output, size_t frames, size_t offset) {
    for (auto & [ id, voice ] : voices_) {
      if (voice->isPlaying()) {
	auto voice_data = voice->render(frames);
	output.mix(voice_data, offset);
      }
    }   
  }

  virtual void applyAftertouch(float aftertouch) {
    for (auto & child : getChildren()) {
      child->applyAftertouch(aftertouch);
    }
  }

  void applyAftertouch(int column, float aftertouch) {
    for (auto & [id, voice] : voices_) {
      if (column == id && voice->isPlaying()) {
	voice->applyAftertouch(aftertouch);
      }
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

  static inline bool is_not_playing(const std::pair<int, std::unique_ptr<TrackState> > & a) { return !a.second->isPlaying(); }

  size_t getVoiceCount() const {
    size_t n = 0;
    if (isPlaying()) n++;
    for (auto & [ id, voice ] : voices_) {
      n += voice->getVoiceCount();
    }
    return n;
  }
  
  size_t getAllocatedVoiceCount() const {
    size_t n = 1;
    for (auto & [ id, voice ] : voices_) {
      n += voice->getAllocatedVoiceCount();
    }
    return n;
  }

  TrackState & addVoice(int identifier, std::unique_ptr<TrackState> voice) {
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), is_not_playing), voices_.end());
    
    voices_.push_back(std::pair(identifier, std::move(voice)));
    return *(voices_.back().second);
  }

  void stopVoices(int column) {
    for (auto & [id, voice] : voices_) {
      if (column == id && voice->isPlaying()) {
	voice->stopNote();
      }
    }
  }

  ChannelConfiguration getChannelConfiguration() const { return channel_config_; }

  void addChild(std::unique_ptr<TrackState> child) { children_.push_back(std::move(child)); }
  const std::vector<std::unique_ptr<TrackState> > & getChildren() const { return children_; }
  std::vector<std::unique_ptr<TrackState> > & getChildren() { return children_; }

private:
  ChannelConfiguration channel_config_;
  std::vector<std::unique_ptr<TrackState> > children_;
  std::vector<std::pair<int, std::unique_ptr<TrackState> > > voices_;
};

#endif
