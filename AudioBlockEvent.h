#ifndef _AUDIOBLOCKEVENT_H_
#define _AUDIOBLOCKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "AudioBuffer.h"

// Audio thread -> VisualizationThread: one block of audio per rendered
// audio block, carrying the signals VisualizationThread's own analyses
// need (see VisualizationThread.h) so none of this work runs on the
// real-time audio thread itself:
//  - master: the decoded stereo output, for the spectrum FFT. Moved, not
//    copied - Mixer::encode() already returns a fresh, owned AudioBuffer
//    per call (see Player.cpp), so there is no extra copy beyond what
//    already happens today, just a relocation of who consumes it.
//  - raw_bus: the full pre-decode ambisonic bus (however many regular
//    channels are active), for both DirAC directional analysis (which
//    only ever reads the first 4 - W/Y/Z/X, ACN order - via its own
//    regularChannelCount() cap, DiracAnalyzer.cpp) and the raw-channel
//    volume meter (which needs every channel). A genuine copy (not a
//    relocation) - Mixer::getRawBus() returns a reference into the
//    mixer's own persistent buffer, overwritten next block, so it has to
//    be copied out before crossing threads.
//  - aux_a/aux_b: the shared send bus's mono AuxA/AuxB sums (SongState's
//    own aux_a_sum_/aux_b_sum_, SongState.h), for the volume meter's
//    trailing AuxA/AuxB columns - not part of the ambisonic raw_bus at
//    all (see AudioBuffer.h's own Channel/Aux distinction), so carried as
//    two separate single-channel buffers. Also genuine copies, same
//    persistent-buffer-gets-overwritten reasoning as raw_bus.
//
// An empty master AudioBuffer (default-constructed, see AudioBuffer::empty())
// is a sentinel telling VisualizationThread::run() to stop - the same role
// PlaybackControlEvent::TERMINATE plays for the audio thread's own loop
// (see UI::start()); the other fields are irrelevant in that case.
class AudioBlockEvent : public Event {
public:
  AudioBlockEvent(AudioBuffer master, AudioBuffer raw_bus, AudioBuffer aux_a, AudioBuffer aux_b)
    : master_(std::move(master)), raw_bus_(std::move(raw_bus)), aux_a_(std::move(aux_a)), aux_b_(std::move(aux_b)) { }

  void dispatch(EventHandler & evh) override { evh.handleAudioBlockEvent(*this); }

  const AudioBuffer & getMaster() const { return master_; }
  const AudioBuffer & getRawBus() const { return raw_bus_; }
  const AudioBuffer & getAuxA() const { return aux_a_; }
  const AudioBuffer & getAuxB() const { return aux_b_; }

private:
  AudioBuffer master_;
  AudioBuffer raw_bus_;
  AudioBuffer aux_a_;
  AudioBuffer aux_b_;
};

#endif
