#ifndef _SENDBUSPROCESSOR_H_
#define _SENDBUSPROCESSOR_H_

#include "SampleData.h"
#include "effects/MVerb.h"
#include "effects/ChorusEngine.h"

// Owned by SongState (one per playback session, persisting across every
// block - see SongState.h): the shared reverb (fed by the cross-track
// SendA sum) and chorus (fed by the cross-track SendB sum) that Mixer.h's
// own comment anticipates ("Phase 2 adds the reverb/chorus that will,
// tapping tracks directly rather than through the mixer"). Fixed,
// hardcoded parameters for now - no XML surface for the shared bus's own
// sound (unlike the per-track Chorus effect, which does expose its
// ChorusEngine's parameters).
class SendBusProcessor {
 public:
  explicit SendBusProcessor(int sampleRate);

  // send_a_mono/send_b_mono: single-channel (mono) cross-track sums for
  // this block. Always processes, even when both are silent, so the
  // reverb tail and chorus modulation state stay continuous across blocks
  // (same reasoning as AmbisonicBinauralMixer's overlap-add tail).
  SampleData process(const SampleData & send_a_mono, const SampleData & send_b_mono, int frames);

 private:
  MVerb<float> reverb_;  // MIX fixed to 1.0 (fully wet) - dry/wet balance is
                         // already controlled upstream by each track's own
                         // sendA amount, not by this shared instance.
  ChorusEngine chorus_;  // 2 channels, decorrelate = true, mix = 1.0 - same
                         // "fully wet, balance controlled upstream" reasoning.

  // Headroom trim: with sendA/sendB near 1.0 and several overlapping
  // notes, the reverb's own decaying energy from earlier notes stacks
  // with new notes' contributions - unlike a single voice's own dry
  // signal, this shared, persistent bus has no natural ceiling of its
  // own, so without this it can genuinely exceed unity and hard-clip
  // (confirmed directly: sendA=1.0 with a 7-note overlapping pizzicato
  // passage measured peak exactly 1.0 with hundreds of clipped samples -
  // audible as crackle - while the same passage at sendA<=0.3 stayed well
  // under 1.0). Same technique as AmbisonicBinauralMixer's
  // kMasterGainTrim, applied here for the same reason: headroom against
  // summed energy from multiple sources feeding one shared bus.
  static constexpr float kWetTrim = 0.4f;
};

#endif
