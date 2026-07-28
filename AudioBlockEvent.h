#ifndef _AUDIOBLOCKEVENT_H_
#define _AUDIOBLOCKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "SampleData.h"

// Audio thread -> VisualizationThread: one block of audio per rendered
// audio block, carrying two independent signals for the two analyses
// VisualizationThread runs (see VisualizationThread.h):
//  - master: the decoded stereo output, for the spectrum FFT. Moved, not
//    copied - Mixer::encode() already returns a fresh, owned SampleData
//    per call (see Player.cpp), so there is no extra copy beyond what
//    already happens today, just a relocation of who consumes it.
//  - raw_bus: the first up to 4 channels (W/Y/Z/X, ACN order) of the
//    pre-decode ambisonic bus, for DirAC directional analysis. This one
//    genuinely is a fresh copy (not a relocation) - Mixer::getRawBus()
//    returns a reference into the mixer's own persistent buffer,
//    overwritten next block, so it has to be copied out before crossing
//    threads.
//
// An empty master SampleData (default-constructed, see SampleData::empty())
// is a sentinel telling VisualizationThread::run() to stop - the same role
// PlaybackControlEvent::TERMINATE plays for the audio thread's own loop
// (see UI::start()); raw_bus is irrelevant in that case.
class AudioBlockEvent : public Event {
public:
  AudioBlockEvent(SampleData master, SampleData raw_bus) : master_(std::move(master)), raw_bus_(std::move(raw_bus)) { }

  void dispatch(EventHandler & evh) override { evh.handleAudioBlockEvent(*this); }

  const SampleData & getMaster() const { return master_; }
  const SampleData & getRawBus() const { return raw_bus_; }

private:
  SampleData master_;
  SampleData raw_bus_;
};

#endif
