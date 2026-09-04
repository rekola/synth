#ifndef _RECORDINGLATENCYEVENT_H_
#define _RECORDINGLATENCYEVENT_H_

#include "Event.h"
#include "EventHandler.h"

// The round-trip output+input delay (AudioAPI::getPlaybackDelayFrames() +
// getCaptureDelayFrames()), measured once by Player.cpp the instant a take
// actually engages while the transport is playing, and carried across to
// the UI thread the same way every other audio-thread-to-UI-thread
// crossing here already works - a plain event on ui_event_queue, not a
// direct cross-thread field write. UI::handleRecordingLatencyEvent() is
// what actually creates, trims, and places the take's own Clip using it.
class RecordingLatencyEvent : public Event {
public:
  RecordingLatencyEvent(int latency_frames) : latency_frames_(latency_frames) { }

  void dispatch(EventHandler & evh) override { evh.handleRecordingLatencyEvent(*this); }

  int getLatencyFrames() const { return latency_frames_; }

private:
  int latency_frames_;
};

#endif
