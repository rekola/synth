#ifndef _PLAYBACKEVENT_H_
#define _PLAYBACKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "SampleData.h"
#include "PlaybackInfo.h"

class PlaybackEvent : public Event {
public:
  PlaybackEvent(const PlaybackInfo & info) : info_(info) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackEvent(*this); }
  
  const SampleData & getData() const { return data_; }
  void setData(SampleData data) { data_ = std::move(data); }

  void setLoudness(std::pair<float, float> loudness) { loudness_ = loudness; }
  const std::pair<float, float> & getLoudness() const { return loudness_; }

  void setInfo(const PlaybackInfo & info) { info_ = info; }
  const PlaybackInfo & getInfo() const { return info_; }
  
private:
  SampleData data_;
  std::pair<float, float> loudness_;
  PlaybackInfo info_;
};

#endif
