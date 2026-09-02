#ifndef _SAMPLETRACKSTATE_H_
#define _SAMPLETRACKSTATE_H_

#include "InstrumentTrackState.h"

class Clip;

// Runtime counterpart of SampleTrack - plays one Clip's own raw audio at a
// time (triggerClip() below), rather than resolving pattern-driven notes
// through an InstrumentPool the way an ordinary InstrumentTrackState does.
// Inherits stopVoices()/stopAllVoices()/mute/solo/sends/position from
// InstrumentTrackState instead of reimplementing them, but triggerClip()
// builds and adds its own voice directly (SampleTrack.cpp) rather than
// going through noteOn() - a raw sample clip needs no Instrument/Track
// indirection resolved for it.
//
// Declared in its own header rather than kept local to SampleTrack.cpp's
// anonymous namespace (unlike those two siblings): triggerClip() is
// called from SongState.h (transport-driven playback) and Player.cpp
// (Session-view live triggering), both outside SampleTrack.cpp, so it
// needs a name those call sites can dynamic_cast to.
class SampleTrackState : public InstrumentTrackState {
public:
  explicit SampleTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, const SphericalPosition & position, const SendLevels & sends)
    : InstrumentTrackState(channel_config, solo, muted, track_id, -1, position, sends) { }

  // No pool index to resolve (a SampleTrack has no instrument_id_ - see
  // SampleTrack.h), same reasoning DrumMachineTrackState/
  // PercussionTrackState already have for their own override. Never
  // actually reached - triggerClip() below never calls noteOn() at all,
  // so nothing ever asks this for an instrument to resolve; overridden
  // anyway so nothing accidentally falls through to InstrumentTrackState's
  // own instrument_id_-based default.
  const Track * getInstrumentSource(const InstrumentPool &) const override { return nullptr; }

  // InstrumentTrackState::render(frames, instruments, context)'s own
  // chunked loop only ever calls renderVoices() when getInstrumentSource()
  // resolves to something - a gate that exists to process pending
  // pattern note-on/off events, which a SampleTrack never has (SongState.h's
  // own scheduling loop never pushes any for TrackType::SAMPLE). Since
  // getInstrumentSource() above always returns nullptr, that gate would
  // silently skip rendering every voice triggerClip() already added.
  // pending_events_/pending_azimuth_ are always empty here, so this is
  // exactly the base class's own single-chunk case, just without the
  // irrelevant instrument-resolution machinery around it.
  AudioBuffer render(int frames, const InstrumentPool &, RenderContext & context) override {
    clearFinishedVoices();
    auto data = renderVoices(frames);
    data.setBpm(context.getBpm());
    setTrackInfo(TrackInfo(isActive(), data.isClipping(), data.calculateMainRMS()));
    return data;
  }

  // Starts playing `clip`'s own audio (its SampleContent, bounded by its
  // in/out trim points) from the beginning, as a single SampleClipVoice
  // (SampleTrack.cpp) added directly via addVoice() - column 0 always, a
  // SampleTrack only ever has one clip playing at a time (Session view's
  // own one-clip-per-track triggering, mirrored by transport playback -
  // see SongState.h's own clip-transition handling). A no-op if `clip`
  // carries no sample content, or its buffer is empty.
  void triggerClip(const Clip & clip);
};

#endif
