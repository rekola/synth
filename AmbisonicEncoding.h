#ifndef _AMBISONICENCODING_H_
#define _AMBISONICENCODING_H_

#include "ChannelConfiguration.h"
#include "SphericalPosition.h"
#include "SampleData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cassert>
#include <unordered_map>

// Ambisonic encode/decode helpers, AmbiX convention: ACN channel ordering,
// SN3D normalization. Support is capped at 2nd order (9 channels) - a hard
// ceiling, not a stepping stone to 3rd order. ACN order is W, Y, Z, X, then
// the degree-2 terms ACN4-8 (channel indices 0-8) - NOT the more visually
// obvious W, X, Y, Z, ... - get this wrong and left/right or up/down
// silently swap.

constexpr int kAmbisonicOrder = 2;
constexpr int kAmbisonicChannelCount = 9; // exactly order 2, not a
                                           // variable-order formula
constexpr int ambisonicChannelCount(int order) { return (order + 1) * (order + 1); }

// SN3D reference gain for the W (ACN0) channel: a plane wave arriving from
// the encoded direction has unity gain here, per the "N_0^0 = 1" SN3D
// convention (AmbiX uses SN3D, not FuMa - FuMa is the convention that
// scales W by 1/sqrt(2) relative to the other channels, which this used to
// mistakenly do; confirmed via a 4-stage diagnostic - encoder gains vs.
// reference SN3D formulas, 9-channel bus content, decode-matrix
// concentration, end-to-end front/back/left/right renders - documented in
// docs/known_bugs.md's history). Shared between computeAmbisonicGains()
// (the encoder) and decodeToStereo() (whose kBoresightNormalization is
// algebraically derived from this) so the two can't drift out of sync
// again the way the old two independently-declared 1/sqrt(2) constants
// could have.
constexpr float kAmbisonicReferenceGain = 1.0f;

// Gains indexed in ACN order (W, Y, Z, X, Acn4..Acn8), not positional order.
// Always computes all 9 degree-0/1/2 gains regardless of the caller's
// actual ambisonic order - callers write however many fit into their real
// output (see AmbisonicVoiceEncoder).
using AmbisonicGains = std::array<float, kAmbisonicChannelCount>;

// SN3D gains for a source at `position` - direction only (azimuth/elevation).
// Does NOT include distance-based attenuation: each voice pre-applies its
// own 1/distance dry-signal attenuation before this encoding (see
// InstrumentVoice::getDistanceGain()) so that attenuation also applies
// uniformly to STEREO/MONO buses, not just AMBISONIC, and so sends (which
// deliberately do NOT attenuate with distance - see SendBusProcessor) aren't
// affected by this function at all. distance <= 0 is still checked here
// purely as a "no position was ever set" marker (most existing tracks never
// set one) - returns a fixed-gain, W-only (diffuse/omnidirectional) result
// instead of running the directional formula, so "no position set" sounds
// centered/enveloping rather than pulled to a single point. Does NOT include
// the actual audio sample, just the per-channel multipliers to apply to it
// (see AmbisonicVoiceEncoder).
inline AmbisonicGains computeAmbisonicGains(const SphericalPosition & position) {
  if (position.distance <= 0.0f) {
    return { kAmbisonicReferenceGain, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  }
  constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
  constexpr float kSqrt3Over2 = 0.86602540378443864676f;
  float az = position.azimuth * kDeg2Rad;
  float el = position.elevation * kDeg2Rad;
  float cos_el = cosf(el);
  float sin_el = sinf(el);

  AmbisonicGains g;
  g[0] = kAmbisonicReferenceGain;                            // W  (ACN0)
  g[1] = sinf(az) * cos_el;                                  // Y  (ACN1)
  g[2] = sin_el;                                             // Z  (ACN2)
  g[3] = cosf(az) * cos_el;                                  // X  (ACN3)
  g[4] = kSqrt3Over2 * cos_el * cos_el * sinf(2.0f * az);    // Acn4
  g[5] = kSqrt3Over2 * sinf(2.0f * el) * sinf(az);           // Acn5
  g[6] = (3.0f * sin_el * sin_el - 1.0f) * 0.5f;             // Acn6
  g[7] = kSqrt3Over2 * sinf(2.0f * el) * cosf(az);           // Acn7
  g[8] = kSqrt3Over2 * cos_el * cos_el * cosf(2.0f * az);    // Acn8
  return g;
}

// Per-voice encoding state: linearly interpolates gains across a block (so a
// moving/newly-triggered source doesn't zipper) and writes/accumulates into
// `out`'s regular (non-send) ACN channels. `mono` must have exactly
// `frames` samples; `out`'s regular channel count (numberOfChannels() minus
// however many of SendA/SendB are present - see SampleData::sendCount()) is
// generally >= 2; channels beyond however many `out` actually has, e.g. a
// 2-channel accumulator lacking Z/X, are simply skipped rather than
// asserting, so this also works as a degenerate 2-channel W/Y-only encode
// if ever needed. Regular channels always occupy `out`'s first N raw
// indices, whether or not sends follow them (see SampleData's presence
// ordering), so plain positional indexing here is correct.
class AmbisonicVoiceEncoder {
 public:
  void encodeBlock(SampleData & out, const float * mono, int frames, const AmbisonicGains & target) {
    if (!seeded_) {
      prev_ = target;
      seeded_ = true;
    }

    int regular = out.numberOfChannels() - out.sendCount();
    int n = std::min(regular, static_cast<int>(target.size()));

    float * channels[kAmbisonicChannelCount] = {};
    for (int c = 0; c < n; c++) channels[c] = out.getChannelData(c);

    for (int i = 0; i < frames; i++) {
      float t = frames > 1 ? static_cast<float>(i) / static_cast<float>(frames - 1) : 1.0f;
      float s = mono[i];
      for (int c = 0; c < n; c++) {
        float g = prev_[static_cast<size_t>(c)] + (target[static_cast<size_t>(c)] - prev_[static_cast<size_t>(c)]) * t;
        channels[c][i] += g * s;
      }
    }

    prev_ = target;
  }

 private:
  AmbisonicGains prev_{};
  bool seeded_ = false;
};

// Cheap cardioid L/R decode of a B-format bus to plain stereo - the
// AmbisonicStereoMixer's decode matrix. Virtual cardioid microphones aimed
// at azimuth -90 (left) / +90 (right), elevation 0: at those azimuths
// cos(az) == 0, so only W and Y contribute (higher-order channels, if
// present, don't). Scaled so a hard-left/right source (computeAmbisonicGains
// at distance 1) decodes back to unity gain. W/Y are always raw channels 0
// and 1 of an ambisonic buffer regardless of order (see Channel's
// declaration order), so plain positional indexing is correct here too.
inline void decodeToStereo(const SampleData & in, SampleData & out) {
  assert(in.numberOfChannels() >= 2);
  assert(out.numberOfChannels() == 2);
  // Derived, not hardcoded, from the shared kAmbisonicReferenceGain - stays
  // correct automatically if that constant is ever revisited again.
  constexpr float kBoresightNormalization = 1.0f / (kAmbisonicReferenceGain * kAmbisonicReferenceGain + 1.0f);

  int n = in.numberOfFrames();
  auto w = in.getChannelData(0);
  auto y = in.getChannelData(1);
  auto left = out.getChannelData(0);
  auto right = out.getChannelData(1);

  for (int i = 0; i < n; i++) {
    float wy = kAmbisonicReferenceGain * w[i];
    left[i] = kBoresightNormalization * (wy - y[i]);
    right[i] = kBoresightNormalization * (wy + y[i]);
  }
}

// The mirror of decodeToStereo(): treats a stereo signal's left/right
// channels as point sources at azimuth mp90/+90, elevation 0, distance 1,
// and sums their contributions into `out`'s ambisonic channels (added, not
// overwritten - callers zero `out` first if that's not wanted). Used when
// an effect's own true format is AMBISONIC but it had to process its
// (reduced) children in real stereo. At az = -90/+90, computeAmbisonicGains's
// higher-order terms are all exactly zero, so only W/Y are ever written.
inline void encodeStereoAsPoints(const SampleData & stereo, SampleData & out) {
  assert(stereo.numberOfChannels() == 2);
  assert(out.numberOfChannels() >= 2);

  static const AmbisonicGains left_gains = computeAmbisonicGains(SphericalPosition{ -90.0f, 0.0f, 1.0f });
  static const AmbisonicGains right_gains = computeAmbisonicGains(SphericalPosition{ 90.0f, 0.0f, 1.0f });

  int n = stereo.numberOfFrames();
  auto left = stereo.getChannelData(0);
  auto right = stereo.getChannelData(1);
  auto out_w = out.getChannelData(0);
  auto out_y = out.getChannelData(1);

  for (int i = 0; i < n; i++) {
    float l = left[i], r = right[i];
    out_w[i] += left_gains[0] * l + right_gains[0] * r;
    out_y[i] += left_gains[1] * l + right_gains[1] * r;
  }
}

// AMBISONIC -> STEREO, otherwise unchanged. Used by effects whose DSP
// genuinely needs real stereo width or is nonlinear (Reverb, Compressor,
// Distortion) to request that format from their children, both at
// tree-construction time (Track::getChildChannelConfiguration) and at
// render time (their State classes' render()).
inline ChannelConfiguration reduceForEffect(const ChannelConfiguration & config) {
  if (config.getType() == ChannelConfiguration::AMBISONIC) {
    return ChannelConfiguration(ChannelConfiguration::STEREO, config.getAudioOutSampleRate());
  }
  return config;
}

// AMBISONIC -> MONO, otherwise unchanged. Used only by the leaf instrument
// classes (Oscilator, Noise, SoundFontInstrument, FileInstrument) right
// before constructing their own voice - a voice only ever needs a mono dry
// signal to be encoded externally, never real stereo width, so this is a
// separate rule from reduceForEffect even though the shape is identical.
inline ChannelConfiguration reduceForPositionalGroup(const ChannelConfiguration & config) {
  if (config.getType() == ChannelConfiguration::AMBISONIC) {
    return ChannelConfiguration(ChannelConfiguration::MONO, config.getAudioOutSampleRate());
  }
  return config;
}

// Shared per-child encode-and-sum helper: holds one AmbisonicVoiceEncoder's
// gain-interpolation state per active child, keyed by whatever stable id the
// caller already has on hand (TrackState's integer child ids; a raw
// TrackState* is just as fine a key for InstrumentTrackState's per-column
// voice vectors, which have no integer id of their own - the pointer is
// stable for exactly the voice's lifetime, same as the state it's keying).
// Used by TrackState::render(int frames)'s generic default and by
// InstrumentTrackState's analogous voices_-based loop. `rendered`'s leading
// channel is always the mono positional dry signal - under an AMBISONIC
// ambient format, the only way a child ends up narrower than its parent is
// via reduceForPositionalGroup (always MONO) at a leaf voice - reduceForEffect
// narrows to STEREO, but effects that use it always re-encode back to
// their own true (ambisonic) format before returning, so a raw STEREO
// result is never seen by a generic parent. 0, 1, or 2 further trailing
// channels may follow the mono signal - SendA and/or SendB, whichever this
// voice actually produced (see SampleData's Channel enum) - which bypass
// spatial gain-encoding entirely and are straight-summed into `accumulator`,
// since a send is not a positional signal.
class PositionalMixer {
 public:
  void encode(const void * id, const SampleData & rendered, const SphericalPosition & position, SampleData & accumulator) {
    assert(rendered.numberOfChannels() == 1 + rendered.sendCount());

    auto & encoder = encoders_[id];
    encoder.encodeBlock(accumulator, rendered.getChannelData(0), rendered.numberOfFrames(), computeAmbisonicGains(position));

    for (auto ch : { Channel::SendA, Channel::SendB }) {
      if (rendered.hasChannel(ch) && accumulator.hasChannel(ch)) {
        auto src = rendered.getChannel(ch);
        auto dst = accumulator.getChannel(ch);
        for (int i = 0; i < rendered.numberOfFrames(); i++) dst[i] += src[i];
      }
    }
  }

  void remove(const void * id) { encoders_.erase(id); }

 private:
  std::unordered_map<const void *, AmbisonicVoiceEncoder> encoders_;
};

#endif
