#ifndef _SENDBUSPROCESSOR_H_
#define _SENDBUSPROCESSOR_H_

#include "../SampleData.h"
#include "../ChannelConfiguration.h"
#include "../AmbisonicEncoding.h"
#include "FDNReverb.h"
#include "ChorusBusEffect.h"

#include <array>

// Owned by SongState (one per playback session, persisting across every
// block - see SongState.h): the shared spatial reverb (fed by the
// cross-track SendA sum) and chorus (fed by the cross-track SendB sum)
// that Mixer.h's own comment anticipates ("Phase 2 adds the reverb/chorus
// that will, tapping tracks directly rather than through the mixer").
//
// This class's own output is *always* ambisonic-shaped (config.
// numberOfChannels() - 4 at order 1, 9 at order 2 for every real top-level
// song config, since ChannelConfiguration::STEREO doesn't exist at all -
// see ambisonic_channels_), never a plain stereo signal. SongState
// accumulates it directly into the top-level mixer, which is guaranteed to
// be ambisonic-shaped too - this class itself has no notion of "stereo
// bus" at all.
//
// The reverb (FDNReverb) produces 8 decorrelated mono tap outputs, each
// encoded here into the bus at a fixed cube-vertex direction (see
// AmbisonicEncoding.h's cubeVertexDirections()) - the reverb return is
// therefore inherently spatial, spread over the whole sphere, and sits
// *before* whatever decoder (binaural/stereo/future) renders the bus, so
// none of this class is decoder-specific. The chorus (ChorusBusEffect)
// still produces a stereo signal internally, folded into the same
// ambisonic accumulator via encodeStereoAsPoints() (fixed az ±90° points -
// unchanged from how the chorus reached the bus before this class existed).
class SendBusProcessor {
 public:
  explicit SendBusProcessor(const ChannelConfiguration & config);

  // Song-level parameters (see Song.h's reverb* getters) - static, set
  // once when a song loads (SongState::initialize()), not continuously
  // automatable (no mechanism for that exists in this codebase today for
  // any effect parameter). Safe to call at any time regardless, including
  // mid-playback - see FDNReverb::setParameters().
  void setReverbParameters(float size, float decayRT60Seconds, float damping, float preDelaySeconds, float wetLevel);

  // send_a_mono/send_b_mono: single-channel (mono) cross-track sums for
  // this block. Always processes, even when both are silent, so the
  // reverb tail and chorus modulation state stay continuous across blocks
  // (same reasoning as AmbisonicBinauralMixer's overlap-add tail).
  void process(const SampleData & send_a_mono, const SampleData & send_b_mono, int frames);

  const SampleData & getBusAmbisonic() const { return bus_ambisonic_; }

 private:
  FDNReverb reverb_;
  ChorusBusEffect chorus_;

  float wet_level_ = 0.2512f; // reverb return level (-12dB default) - see setReverbParameters()

  // config.numberOfChannels() - 4 or 9 for every real top-level AMBISONIC
  // config. The only other value ever seen here is 1, for the synthetic
  // top-level MONO config one regression test constructs directly
  // (bypassing MixerFactory) - SongState only ever calls process()/
  // getBusAmbisonic() when its own config is AMBISONIC, so that case never
  // actually reaches this class's process() method in practice. Fixed for
  // this instance's lifetime.
  int ambisonic_channels_;

  // The 8 cube-vertex directions' encode gains, computed once at
  // construction via the same computeAmbisonicGains() voices use - fixed
  // for this instance's lifetime, so no per-block gain-interpolation
  // machinery (AmbisonicVoiceEncoder) is needed, unlike a moving voice.
  std::array<AmbisonicGains, FDNReverb::kNumLines> tap_gains_;

  // Headroom trim on the chorus path only, preserving its pre-existing
  // (pre-FDN-reverb) clipping behavior unchanged - see docs/known_bugs.md's
  // history for how this exact value was measured (sendA=1.0, 7-note
  // overlapping pizzicato: peak exactly 1.0/hundreds of clipped samples
  // without it). The reverb path's own headroom is now the user-facing
  // wet_level_ parameter instead of a hidden constant.
  static constexpr float kChorusWetTrim = 0.4f;

  SampleData bus_ambisonic_;    // always ambisonic_channels_ channels
  SampleData chorus_trimmed_;   // scratch: chorus_'s stereo output x kChorusWetTrim, before encodeStereoAsPoints()
};

#endif
