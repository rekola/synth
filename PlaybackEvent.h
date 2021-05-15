#ifndef _PLAYBACKEVENT_H_
#define _PLAYBACKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "SampleData.h"
#include "PlaybackInfo.h"

class PlaybackEvent : public Event {
public:
  PlaybackEvent(const PlaybackInfo & _info) : info(_info) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackEvent(*this); }
  
  const SampleData & getData() const { return data; }
  void setData(SampleData _data) { data = _data; }

  void setLoudness(std::pair<float, float> _loudness) { loudness = _loudness; }
  const std::pair<float, float> & getLoudness() const { return loudness; }

  void setInfo(const PlaybackInfo & _info) { info = _info; }
  const PlaybackInfo & getInfo() const { return info; }
  
private:
  SampleData data;
  std::pair<float, float> loudness;
  PlaybackInfo info;
};

#endif
