#ifndef _EVENTHANDLER_H_
#define _EVENTHANDLER_H_

#include "Event.h"

class PlaybackControlEvent;
class InputEvent;
class MidiEvent;
class LaunchpadPadEvent;
class LaunchpadButtonEvent;
class LaunchpadChannelPressureEvent;
class AudioBlockEvent;
class VisualizationResultEvent;

class EventHandler {
 public:
  EventHandler() { }
  virtual ~EventHandler() { }

  void handleEvent(Event & event) { event.dispatch(*this); }

  virtual void handlePlaybackControlEvent(PlaybackControlEvent & ev) {}
  virtual void handleInputEvent(InputEvent & ev) { }
  virtual void handleMidiEvent(MidiEvent & ev) { }
  virtual void handleLaunchpadPadEvent(LaunchpadPadEvent & ev) { }
  virtual void handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) { }
  virtual void handleLaunchpadChannelPressureEvent(LaunchpadChannelPressureEvent & ev) { }
  virtual void handleAudioBlockEvent(AudioBlockEvent & ev) { }
  virtual void handleVisualizationResultEvent(VisualizationResultEvent & ev) { }
};

#endif
