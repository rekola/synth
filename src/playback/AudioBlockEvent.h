#ifndef _AUDIOBLOCKEVENT_H_
#define _AUDIOBLOCKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "../audio/AudioBuffer.h"

// Audio thread -> VisualizationThread: one block of audio per rendered
// audio block, carrying the signals VisualizationThread's own analyses
// need (see VisualizationThread.h) so none of this work runs on the
// real-time audio thread itself.
//
// Every scope (the volume meter's Main/AuxA/AuxB columns, the spectrum
// FFT, DirAC) shows one single buffer's own output - the currently
// *active* one - never a cross-buffer mix: AuxA/AuxB in particular can't
// be combined meaningfully across buffers (each buffer's own
// SendBusProcessor can map them to a completely different BusEffect - see
// BusEffectRegistry), so once that's true for the Aux columns, the Main
// column right next to them in the same meter has to follow the same rule
// or the two would be describing different things in one view (see the
// per-buffer editing/playback-state plan's Part B for the fuller
// reasoning). raw_bus/aux_a/aux_b below are therefore the *active*
// buffer's own solo contribution, not the combined signal actually being
// played:
//  - raw_bus: the active buffer's own pre-decode ambisonic bus (however
//    many regular channels are active) - Player.cpp gets this by
//    rendering the active buffer's own SongState into the shared Mixer
//    *first* each block (right after reset()) and snapshotting
//    Mixer::getRawBus() at that exact moment, before any other live
//    buffer's own output has been accumulated into it - a genuine copy of
//    that snapshot, not a reference (the mixer's own accumulator keeps
//    changing as the rest of the block's rendering continues). Feeds both
//    DirAC directional analysis (which only ever reads the first 4 -
//    W/Y/Z/X, ACN order - via its own regularChannelCount() cap,
//    DiracAnalyzer.cpp) and the raw-channel volume meter (which needs
//    every channel). Empty (0 regular channels, but still correctly
//    frame-sized) when the active buffer has no live SongState at all -
//    nothing playing/auditioned on it yet.
//  - aux_a/aux_b: the active buffer's own SongState::getAuxASum()/
//    getAuxBSum() (each mono), for the volume meter's trailing AuxA/AuxB
//    columns - not part of the ambisonic raw_bus at all (see
//    AudioBuffer.h's own Channel/Aux distinction), so carried as two
//    separate single-channel buffers. Silent (but correctly frame-sized)
//    under the same no-live-SongState condition as raw_bus above.
//  - master: the *true* combined decoded stereo output (every live
//    buffer's own contribution, accumulated before a single decode - the
//    actual signal audio.play() sends to the device) - kept here purely
//    as the empty-buffer shutdown sentinel below, the same buffer
//    Player.cpp already computes for real playback and would otherwise
//    just discard, moved rather than copied. VisualizationThread no
//    longer spectrum-analyzes this directly (that would defeat the whole
//    "one buffer only" rule above) - it decodes its own scratch view of
//    raw_bus instead, for the FFT specifically, off this real-time thread
//    (see VisualizationThread.cpp).
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
