#ifndef _EVENTHANDLER_H_
#define _EVENTHANDLER_H_

#include "Event.h"

class PlaybackControlEvent;
class InputEvent;
class AudioBlockEvent;

class EventHandler {
 public:
  EventHandler() { }
  virtual ~EventHandler() { }

  void handleEvent(Event & event) { event.dispatch(*this); }

  virtual void handlePlaybackControlEvent(PlaybackControlEvent & ev) {}
  virtual void handleInputEvent(InputEvent & ev) {}
  virtual void handleAudioBlockEvent(AudioBlockEvent & ev) {}
};

#endif
