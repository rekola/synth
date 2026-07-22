#ifndef _SENDBUSPROCESSOR_H_
#define _SENDBUSPROCESSOR_H_

#include "../SampleData.h"
#include "../ChannelConfiguration.h"
#include "../AmbisonicEncoding.h"
#include "FDNReverb.h"
#include "MultiTapDelay.h"

#include <array>

// Owned by SongState (one per playback session, persisting across every
// block - see SongState.h): the shared spatial reverb (fed by the
// cross-track SendA sum) and multi-tap delay (fed by the cross-track SendB
// sum) that Mixer.h's own comment anticipates ("Phase 2 adds the reverb/
// chorus that will, tapping tracks directly rather than through the
// mixer").
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
// none of this class is decoder-specific. The delay (MultiTapDelay)
// produces 4 mono tap outputs at fixed-or-evolving directions (see
// MultiTapDelay.h) - each encoded here the same way, via its own
// AmbisonicVoiceEncoder so a pattern-mode direction change interpolates
// smoothly across a block instead of clicking (the reverb's fixed
// directions never need this).
class SendBusProcessor {
 public:
  explicit SendBusProcessor(const ChannelConfiguration & config);

  // Song-level parameters (see Song.h's reverb*/delay* getters) - static,
  // set once when a song loads (SongState::initialize()), not continuously
  // automatable (no mechanism for that exists in this codebase today for
  // any effect parameter). Safe to call at any time regardless, including
  // mid-playback - see FDNReverb::setParameters()/MultiTapDelay::setParameters().
  void setReverbParameters(float size, float decayRT60Seconds, float damping, float preDelaySeconds, float wetLevel);
  void setDelayParameters(float baseRows, float feedbackGain, float damping, DelayPattern pattern, float patternSpeed, float wetLevel, float rowDurationSeconds);

  // send_a_mono/send_b_mono: single-channel (mono) cross-track sums for
  // this block. Always processes, even when both are silent, so the
  // reverb tail and delay feedback/pattern state stay continuous across
  // blocks (same reasoning as AmbisonicBinauralMixer's overlap-add tail).
  void process(const SampleData & send_a_mono, const SampleData & send_b_mono, int frames);

  const SampleData & getBusAmbisonic() const { return bus_ambisonic_; }

 private:
  FDNReverb reverb_;
  MultiTapDelay delay_;

  float wet_level_ = 0.2512f; // reverb return level (-12dB default) - see setReverbParameters()
  float delay_wet_ = 0.354f;  // delay return level (-9dB default) - see setDelayParameters()

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
  // machinery (AmbisonicVoiceEncoder) is needed, unlike the delay's
  // feedback tap (which can move).
  std::array<AmbisonicGains, FDNReverb::kNumLines> tap_gains_;

  // One AmbisonicVoiceEncoder per delay tap - all 4 get the same
  // smoothly-interpolated-across-a-block treatment for simplicity, even
  // though only the feedback tap (index MultiTapDelay::kNumTaps - 1) ever
  // actually changes direction; for the other 3, interpolation is a no-op
  // after the first block since their target never changes.
  std::array<AmbisonicVoiceEncoder, MultiTapDelay::kNumTaps> delay_tap_encoders_;

  SampleData bus_ambisonic_;    // always ambisonic_channels_ channels
};

#endif
