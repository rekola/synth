#ifndef _THRESHOLDRECORDINGTRIGGEREDEVENT_H_
#define _THRESHOLDRECORDINGTRIGGEREDEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "../audio/AudioBuffer.h"

// Pushed by Player.cpp's poll loop the instant captured input crosses the
// loudness-threshold-armed recording trigger - carries everything UI::
// handleThresholdRecordingTriggeredEvent() needs to start the take as if
// it had begun this whole time ago: `preroll` is the ring buffer's own
// already-captured lead-in (RecordingRingBuffer::drain(), chronological
// order, ready to prepend as-is), and `scene`/`row` are the transport's
// own position backdated by that same pre-roll span - computed here, on
// the audio thread (the only place that already has direct, same-thread
// access to the recording buffer's own live SongState), rather than
// written directly into Controller (which owns this position the same
// way it owns every other recording-position field - see Controller::
// armRecordingStart()'s own comment on why that crossing always goes
// through an event, never a direct cross-thread field write).
class ThresholdRecordingTriggeredEvent : public Event {
public:
  ThresholdRecordingTriggeredEvent(int track_id, AudioBuffer preroll, int scene, int row)
    : track_id_(track_id), preroll_(std::move(preroll)), scene_(scene), row_(row) { }

  void dispatch(EventHandler & evh) override { evh.handleThresholdRecordingTriggeredEvent(*this); }

  int getTrackId() const { return track_id_; }
  const AudioBuffer & getPreroll() const { return preroll_; }
  int getScene() const { return scene_; }
  int getRow() const { return row_; }

private:
  int track_id_;
  AudioBuffer preroll_;
  int scene_, row_;
};

#endif
