#ifndef _EVENTHANDLER_H_
#define _EVENTHANDLER_H_

#include "Event.h"

class PlaybackEvent;
class PlaybackControlEvent;
class LogEvent;

class EventHandler {
 public:
  EventHandler() { }
  virtual ~EventHandler() { }

  void handleEvent(Event & event) { event.dispatch(*this); }

  virtual void handlePlaybackEvent(PlaybackEvent & ev) { }
  virtual void handlePlaybackControlEvent(PlaybackControlEvent & ev) { }
  virtual void handleLogEvent(LogEvent & ev) { }
};

#endif
