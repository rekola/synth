#ifndef _SAMPLETRACKSTATE_H_
#define _SAMPLETRACKSTATE_H_

#include "InstrumentTrackState.h"

class Clip;

// Runtime counterpart of SampleTrack - plays one Clip's own raw audio at a
// time (triggerClip() below), rather than resolving pattern-driven notes
// through an InstrumentPool the way an ordinary InstrumentTrackState does.
// Inherits noteOn()/stopVoices()/stopAllVoices()/mute/solo/sends/position
// from InstrumentTrackState instead of reimplementing them - the same
// machinery DrumMachineTrackState/PercussionTrackState already reuse for
// their own not-quite-ordinary triggering.
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
  // SampleTrack.h) - same reasoning DrumMachineTrackState/
  // PercussionTrackState already have for their own override. Never
  // actually reached: triggerClip() below builds its own on-the-fly
  // adapter instrument directly rather than going through noteOn()'s
  // usual getInstrumentSource()-resolved path - overridden anyway so
  // nothing accidentally falls through to InstrumentTrackState's own
  // instrument_id_-based default.
  const Track * getInstrumentSource(const InstrumentPool &) const override { return nullptr; }

  // Starts playing `clip`'s own audio (Clip::getSample(), bounded by its
  // in/out trim points) from the beginning, as a single voice - spawned
  // via the base class's own noteOn() (retriggerVoices()/
  // chokeExclusiveClasses() included), just fed a freshly-built adapter
  // instrument (SampleTrack.cpp) instead of one resolved from the pool.
  // column 0 always - a SampleTrack only ever has one clip playing at a
  // time (Session view's own one-clip-per-track triggering, mirrored by
  // transport playback - see SongState.h's own clip-transition handling).
  // A no-op if `clip` carries no sample, or its buffer is empty.
  void triggerClip(const Clip & clip);
};

#endif
