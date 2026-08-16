#ifndef _PLAYBACKEVENT_H_
#define _PLAYBACKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "PlaybackInfo.h"

class PlaybackEvent : public Event {
public:
  PlaybackEvent(const PlaybackInfo & info) : info_(info) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackEvent(*this); }

  void setInfo(PlaybackInfo info) { info_ = std::move(info); }
  const PlaybackInfo & getInfo() const { return info_; }

private:
  PlaybackInfo info_;
};

#endif
