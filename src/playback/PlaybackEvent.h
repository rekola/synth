#ifndef _PLAYBACKEVENT_H_
#define _PLAYBACKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "../state/PlaybackInfo.h"

#include <string>
#include <utility>

class PlaybackEvent : public Event {
public:
  // buffer_name says which open buffer `info` is a snapshot of - Player
  // now pushes one of these per live SongState every block (see the
  // per-buffer editing/playback-state plan's Part B), not just one for
  // "the" song, so Controller::receivePlaybackSnapshot() needs to know
  // which buffer's own playback_info this updates.
  PlaybackEvent(std::string buffer_name, const PlaybackInfo & info) : buffer_name_(std::move(buffer_name)), info_(info) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackEvent(*this); }

  const std::string & getBufferName() const { return buffer_name_; }
  void setInfo(PlaybackInfo info) { info_ = std::move(info); }
  const PlaybackInfo & getInfo() const { return info_; }

private:
  std::string buffer_name_;
  PlaybackInfo info_;
};

#endif
