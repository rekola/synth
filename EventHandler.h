#ifndef _EVENTHANDLER_H_
#define _EVENTHANDLER_H_

#include "Event.h"

class PlaybackEvent;
class PlaybackControlEvent;
class LogEvent;
class RecordEvent;
class InputEvent;
class MidiEvent;
class LaunchpadPadEvent;
class LaunchpadButtonEvent;

class EventHandler {
 public:
  EventHandler() { }
  virtual ~EventHandler() { }

  void handleEvent(Event & event) { event.dispatch(*this); }

  virtual void handlePlaybackEvent(PlaybackEvent & ev) { }
  virtual void handlePlaybackControlEvent(PlaybackControlEvent & ev) { }
  virtual void handleLogEvent(LogEvent & ev) { }
  virtual void handleRecordEvent(RecordEvent & ev) { }
  virtual void handleInputEvent(InputEvent & ev) { }
  virtual void handleMidiEvent(MidiEvent & ev) { }
  virtual void handleLaunchpadPadEvent(LaunchpadPadEvent & ev) { }
  virtual void handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) { }
};

#endif
