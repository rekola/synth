#ifndef _AMBISONICENCODING_H_
#define _AMBISONICENCODING_H_

#include "ChannelConfiguration.h"
#include "SphericalPosition.h"
#include "SampleData.h"

#include <cmath>
#include <cassert>
#include <unordered_map>

// First-order-ambisonic (FOA) encode/decode helpers, AmbiX convention: ACN
// channel ordering, SN3D normalization. ACN order for FOA is W, Y, Z, X
// (channel indices 0, 1, 2, 3) - NOT the more visually obvious W, X, Y, Z -
// get this wrong and left/right or up/down silently swap.

constexpr int ambisonicChannelCount(int order) { return (order + 1) * (order + 1); }

// Gains named in ACN order (W, Y, Z, X), not positional order.
struct FoaGains { float w, y, z, x; };

// SN3D gains for a source at `position`, already including 1/distance
// attenuation - does NOT include the actual audio sample, just the
// per-channel multipliers to apply to it (see AmbisonicVoiceEncoder).
// distance <= 0 has no meaningful direction by construction (most existing
// tracks never set a position at all) - returns a fixed-gain, W-only
// (diffuse/omnidirectional) result instead of running the directional
// formula, both to avoid dividing by zero and so "no position set" sounds
// centered/enveloping rather than pulled to a single point.
inline FoaGains computeFoaGains(const SphericalPosition & position) {
  constexpr float kReferenceGain = 0.70710678118654752f; // 1/sqrt(2)
  if (position.distance <= 0.0f) {
    return { kReferenceGain, 0.0f, 0.0f, 0.0f };
  }
  constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
  float az = position.azimuth * kDeg2Rad;
  float el = position.elevation * kDeg2Rad;
  float atten = 1.0f / position.distance;
  float cos_el = cosf(el);
  return {
    kReferenceGain * atten,
    sinf(az) * cos_el * atten,
    sinf(el) * atten,
    cosf(az) * cos_el * atten
  };
}

// Per-voice encoding state: linearly interpolates gains across a block (so a
// moving/newly-triggered source doesn't zipper) and writes/accumulates into
// `out`'s ACN channels. `mono` must have exactly `frames` samples; `out`
// must be an AMBISONIC-sized accumulator (>= 2 channels; channels beyond
// however many `out` actually has, e.g. a 2-channel accumulator lacking
// Z/X, are simply skipped rather than asserting, so this also works as a
// degenerate 2-channel W/Y-only encode if ever needed).
class AmbisonicVoiceEncoder {
 public:
  void encodeBlock(SampleData & out, const float * mono, int frames, const FoaGains & target) {
    if (!seeded_) {
      prev_ = target;
      seeded_ = true;
    }

    auto out_w = out.getChannelData(0);
    auto out_y = out.getChannelData(1);
    auto out_z = out.numberOfChannels() > 2 ? out.getChannelData(2) : nullptr;
    auto out_x = out.numberOfChannels() > 3 ? out.getChannelData(3) : nullptr;

    for (int i = 0; i < frames; i++) {
      float t = frames > 1 ? static_cast<float>(i) / static_cast<float>(frames - 1) : 1.0f;
      float w = prev_.w + (target.w - prev_.w) * t;
      float y = prev_.y + (target.y - prev_.y) * t;
      float s = mono[i];

      out_w[i] += w * s;
      out_y[i] += y * s;
      if (out_z) {
        float z = prev_.z + (target.z - prev_.z) * t;
        out_z[i] += z * s;
      }
      if (out_x) {
        float x = prev_.x + (target.x - prev_.x) * t;
        out_x[i] += x * s;
      }
    }

    prev_ = target;
  }

 private:
  FoaGains prev_{};
  bool seeded_ = false;
};

// Cheap cardioid L/R decode of a B-format bus to plain stereo - the
// AmbisonicStereoMixer's decode matrix. Virtual cardioid microphones aimed
// at azimuth -90 (left) / +90 (right), elevation 0: at those azimuths
// cos(az) == 0, so only W and Y contribute (X/Z, if present, don't).
// Scaled so a hard-left/right source (computeFoaGains at distance 1)
// decodes back to unity gain.
inline void decodeToStereo(const SampleData & in, SampleData & out) {
  assert(in.numberOfChannels() >= 2);
  assert(out.numberOfChannels() == 2);
  constexpr float kReferenceGain = 0.70710678118654752f;
  constexpr float kBoresightNormalization = 2.0f / 3.0f; // 1 / (kReferenceGain^2 + 1)

  int n = in.numberOfFrames();
  auto w = in.getChannelData(0);
  auto y = in.getChannelData(1);
  auto left = out.getChannelData(0);
  auto right = out.getChannelData(1);

  for (int i = 0; i < n; i++) {
    float wy = kReferenceGain * w[i];
    left[i] = kBoresightNormalization * (wy - y[i]);
    right[i] = kBoresightNormalization * (wy + y[i]);
  }
}

// The mirror of decodeToStereo(): treats a stereo signal's left/right
// channels as point sources at azimuth mp90/+90, elevation 0, distance 1,
// and sums their contributions into `out`'s ambisonic channels (added, not
// overwritten - callers zero `out` first if that's not wanted). Used when
// an effect's own true format is AMBISONIC but it had to process its
// (reduced) children in real stereo. At az = -90/+90, computeFoaGains's X
// and Z are exactly zero, so only W/Y are ever written.
inline void encodeStereoAsPoints(const SampleData & stereo, SampleData & out) {
  assert(stereo.numberOfChannels() == 2);
  assert(out.numberOfChannels() >= 2);

  static const FoaGains left_gains = computeFoaGains(SphericalPosition{ -90.0f, 0.0f, 1.0f });
  static const FoaGains right_gains = computeFoaGains(SphericalPosition{ 90.0f, 0.0f, 1.0f });

  int n = stereo.numberOfFrames();
  auto left = stereo.getChannelData(0);
  auto right = stereo.getChannelData(1);
  auto out_w = out.getChannelData(0);
  auto out_y = out.getChannelData(1);

  for (int i = 0; i < n; i++) {
    float l = left[i], r = right[i];
    out_w[i] += left_gains.w * l + right_gains.w * r;
    out_y[i] += left_gains.y * l + right_gains.y * r;
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
// InstrumentTrackState's analogous voices_-based loop. `rendered` is always
// exactly 1 channel here in practice: under an AMBISONIC ambient format,
// the only way a child ends up narrower than its parent is via
// reduceForPositionalGroup (always MONO) at a leaf voice - reduceForEffect
// narrows to STEREO, but effects that use it always re-encode back to
// their own true (ambisonic) format before returning, so a raw STEREO
// result is never seen by a generic parent.
class PositionalMixer {
 public:
  void encode(const void * id, const SampleData & rendered, const SphericalPosition & position, SampleData & accumulator) {
    assert(rendered.numberOfChannels() == 1);
    auto & encoder = encoders_[id];
    encoder.encodeBlock(accumulator, rendered.getChannelData(0), rendered.numberOfFrames(), computeFoaGains(position));
  }

  void remove(const void * id) { encoders_.erase(id); }

 private:
  std::unordered_map<const void *, AmbisonicVoiceEncoder> encoders_;
};

#endif
