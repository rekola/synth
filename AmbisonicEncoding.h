#ifndef _AMBISONICENCODING_H_
#define _AMBISONICENCODING_H_

#include "ChannelConfiguration.h"
#include "SphericalPosition.h"
#include "SampleData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cassert>
#include <vector>

// Ambisonic encode/decode helpers, AmbiX convention: ACN channel ordering,
// SN3D normalization. Support is capped at 3rd order (16 channels) - a hard
// ceiling, not a stepping stone to 4th order (raising it again later needs
// a new degree-4 SH block in computeAmbisonicGains, a new virtual-speaker
// rig satisfying t>=2*4+1=9, and another pass over every place this file's
// own history has already had to touch twice: SampleData's Channel enum,
// AmbisonicBinauralMixer's speaker-layout dispatch and kMasterGainTrim
// headroom derivation). ACN order is W, Y, Z, X, then the degree-2 terms
// ACN4-8, then the degree-3 terms ACN9-15 (channel indices 0-15) - NOT the
// more visually obvious W, X, Y, Z, ... - get this wrong and left/right or
// up/down silently swap.

constexpr int kAmbisonicOrder = 3;
constexpr int ambisonicChannelCount(int order) { return (order + 1) * (order + 1); }
constexpr int kAmbisonicChannelCount = ambisonicChannelCount(kAmbisonicOrder); // 16 at order 3

// The 8 cube-vertex directions: 4 azimuths x 2 elevations, +-35.264
// degrees (atan(1/sqrt(2)), the true cube-vertex angle) - shared by
// AmbisonicBinauralMixer's order-1 8-speaker decode layout and the shared
// send bus's spatial reverb (SendBusProcessor/FDNReverb), which spreads
// its 8 decorrelated taps over these same directions. A single shared
// source of truth rather than two independently-declared copies of the
// same floating-point constants, which could otherwise silently drift
// apart. Azimuth here is in this engine's convention (positive = right,
// see PanLaw.h) - the same convention computeAmbisonicGains() below uses,
// so these values can be passed into it directly.
constexpr float kCubeVertexElevation = 35.264389682754654f; // atan(1/sqrt(2))
struct AmbisonicDirection { float azimuth, elevation; };
inline std::array<AmbisonicDirection, 8> cubeVertexDirections() {
  return {{
    { 45.0f, kCubeVertexElevation }, { 135.0f, kCubeVertexElevation }, { -135.0f, kCubeVertexElevation }, { -45.0f, kCubeVertexElevation },
    { 45.0f, -kCubeVertexElevation }, { 135.0f, -kCubeVertexElevation }, { -135.0f, -kCubeVertexElevation }, { -45.0f, -kCubeVertexElevation },
  }};
}

// The 26-point Lebedev quadrature grid (degree/precision-7 rule) - order 3's
// virtual-speaker rig (order 3 needs design strength t>=2*3+1=7; the
// 12-point icosahedron order 2 uses is only a 5-design, insufficient here).
// Used purely as a well-distributed, independently-published 26-direction
// point set to place speakers at - the quadrature *weights* Lebedev's rule
// also defines are irrelevant, since nothing here is being integrated.
// Per Wikipedia's "Lebedev quadrature" article, the 26 points are the union
// of three octahedral-symmetry orbits:
//  - a1 (6 points): permutations of (+-1,0,0) - an octahedron's vertices.
//  - a2 (12 points): permutations of (+-1,+-1,0)/sqrt(2) - that octahedron's
//    edge midpoints.
//  - a3 (8 points): permutations of (+-1,+-1,+-1)/sqrt(3) - a cube's
//    vertices, which (mapped through this same az/el convention) work out
//    to exactly cubeVertexDirections() above, reused rather than
//    re-derived.
// Converting each Cartesian class to this engine's az/el convention
// (x=front, y=right, z=up - see computeAmbisonicGains) gives closed-form
// values for all 26 points; no approximated/hand-rounded coordinates.
inline std::vector<AmbisonicDirection> lebedev26Directions() {
  std::vector<AmbisonicDirection> result;
  // a1: front/back/right/left/up/down.
  result.insert(result.end(), {
    { 0.0f, 0.0f }, { 180.0f, 0.0f }, { 90.0f, 0.0f }, { -90.0f, 0.0f },
    { 0.0f, 90.0f }, { 0.0f, -90.0f },
  });
  // a2: 4 diagonals at elevation 0 (between adjacent a1 horizontal points),
  // 4 each at +-45 degrees elevation (between an a1 horizontal point and a
  // pole).
  result.insert(result.end(), {
    { 45.0f, 0.0f }, { -45.0f, 0.0f }, { 135.0f, 0.0f }, { -135.0f, 0.0f },
    { 0.0f, 45.0f }, { 180.0f, 45.0f }, { 90.0f, 45.0f }, { -90.0f, 45.0f },
    { 0.0f, -45.0f }, { 180.0f, -45.0f }, { 90.0f, -45.0f }, { -90.0f, -45.0f },
  });
  // a3: identical to cubeVertexDirections().
  auto cube = cubeVertexDirections();
  result.insert(result.end(), cube.begin(), cube.end());
  return result;
}

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
// Always computes all 16 degree-0/1/2/3 gains regardless of the caller's
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
  // Degree-3 SN3D normalization constants - independently re-derived (not
  // transcribed from a single source) from the general real-SH SN3D
  // formula N_{l,m} = sqrt(2*(l-m)!/(l+m)!) for m != 0, applied to the
  // degree-3 associated Legendre terms, and cross-checked against this
  // file's own existing (tested) degree-2 constant: the same general
  // formula reproduces kSqrt3Over2 exactly for both m=1 and m=2 at l=2
  // before being trusted for l=3.
  constexpr float kSqrt5Over8 = 0.79056941504209483300f;  // ACN9/15 (m=+-3)
  constexpr float kSqrt15Over2 = 1.93649167310370844259f; // ACN10/14 (m=+-2)
  constexpr float kSqrt3Over8 = 0.61237243569579452455f;  // ACN11/13 (m=+-1)
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
  g[9]  = kSqrt5Over8 * sinf(3.0f * az) * cos_el * cos_el * cos_el;                    // Acn9  (m=-3)
  g[10] = kSqrt15Over2 * sinf(2.0f * az) * sin_el * cos_el * cos_el;                   // Acn10 (m=-2)
  g[11] = kSqrt3Over8 * sinf(az) * cos_el * (5.0f * sin_el * sin_el - 1.0f);           // Acn11 (m=-1)
  g[12] = sin_el * (5.0f * sin_el * sin_el - 3.0f) * 0.5f;                            // Acn12 (m=0)
  g[13] = kSqrt3Over8 * cosf(az) * cos_el * (5.0f * sin_el * sin_el - 1.0f);           // Acn13 (m=+1)
  g[14] = kSqrt15Over2 * cosf(2.0f * az) * sin_el * cos_el * cos_el;                   // Acn14 (m=+2)
  g[15] = kSqrt5Over8 * cosf(3.0f * az) * cos_el * cos_el * cos_el;                    // Acn15 (m=+3)
  return g;
}

// max-rE ("maximum energy vector") per-degree decode weighting: concentrates
// decoded energy toward the intended direction (less off-axis smear) at
// some cost to raw spatial resolution - the standard trade-off used by most
// production ambisonic decoders. Decode-only (see AmbisonicBinauralMixer's
// use of these) - computeAmbisonicGains() above, the shared encoder, stays
// completely unweighted, since weighting it would corrupt every downstream
// consumer (voices, sends, decode matrices alike) irreversibly; there is no
// way for a consumer that wants the unweighted signal to undo it again.
//
// g_l = P_l(cos theta_E), where P_l is the Legendre polynomial of degree l
// and cos theta_E is the largest root of P_{L+1} (L = ambisonic order).
inline float maxReReferenceCosine(int order) {
  switch (order) {
    case 1: return 0.577350269189625764509f;  // 1/sqrt(3)
    case 2: return 0.774596669241483377036f;  // sqrt(3/5)
    case 3: return 0.861136311594052575224f;  // largest root of P_4
    default:
      assert(false && "maxReReferenceCosine: unsupported order");
      return 1.0f;
  }
}

// Fills outGains[0..order] with the max-rE gain g_l for each degree l, via
// the standard Legendre three-term recurrence (l+1)P_{l+1}(x) =
// (2l+1)x*P_l(x) - l*P_{l-1}(x), evaluated at x = maxReReferenceCosine(order)
// - g_l is always computed generically from that one looked-up root, never
// a hardcoded per-order gain table, so a future order only needs one more
// root added to maxReReferenceCosine(), not new gain-computation code. Does
// NOT apply the separate energy-renormalization scalar (k, mean-square
// gain preservation across channels) - that is folded in by the caller on
// top of these raw g_l values, once per decode matrix construction.
inline void maxReGainsPerDegree(int order, float * outGains) {
  float x = maxReReferenceCosine(order);
  outGains[0] = 1.0f;
  if (order < 1) return;
  outGains[1] = x;
  for (int l = 1; l < order; l++) {
    outGains[l + 1] = ((2.0f * static_cast<float>(l) + 1.0f) * x * outGains[l]
                        - static_cast<float>(l) * outGains[l - 1]) / static_cast<float>(l + 1);
  }
}

// The ACN degree of channel index c (0-based): 0 is degree 0, 1-3 are
// degree 1, 4-8 are degree 2, 9-15 are degree 3, ... (degree l starts at
// index l*l). Exact integer search rather than floor(sqrt(c)) - a float
// sqrt() landing a hair under an exact integer at one of the degree
// boundaries (4, 9, 16, ...) would silently misclassify that channel's
// degree, and this range is small enough that the search costs nothing.
inline int acnDegree(int channelIndex) {
  int l = 0;
  while ((l + 1) * (l + 1) <= channelIndex) l++;
  return l;
}

// Per-voice encoding state: linearly interpolates gains across a block (so a
// moving/newly-triggered source doesn't zipper) and writes/accumulates into
// `out`'s regular (non-aux) ACN channels. `mono` must have exactly
// `frames` samples; `out`'s regular channel count (SampleData::regularChannelCount(),
// i.e. numberOfChannels() minus however many of AuxA/AuxB are present) is
// generally >= 2; channels beyond however many `out` actually has, e.g. a
// 2-channel accumulator lacking Z/X, are simply skipped rather than
// asserting, so this also works as a degenerate 2-channel W/Y-only encode
// if ever needed. Regular channels always occupy `out`'s first N raw
// indices, whether or not aux channels follow them (see SampleData's
// presence ordering), so plain positional indexing here is correct.
class AmbisonicVoiceEncoder {
 public:
  void encodeBlock(SampleData & out, const float * mono, int frames, const AmbisonicGains & target) {
    if (!seeded_) {
      prev_ = target;
      seeded_ = true;
    }

    int regular = out.regularChannelCount();
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
  assert(in.numberOfChannels() >= 1);
  assert(out.numberOfChannels() == 2);
  // Derived, not hardcoded, from the shared kAmbisonicReferenceGain - stays
  // correct automatically if that constant is ever revisited again.
  constexpr float kBoresightNormalization = 1.0f / (kAmbisonicReferenceGain * kAmbisonicReferenceGain + 1.0f);

  int n = in.numberOfFrames();
  auto w = in.getChannelData(0);
  // A genuine 1-channel (MONO, 0th-order-ambisonic) bus has no Y - treat it
  // as 0, which makes left == right below: an equal broadcast of the
  // non-directional W signal, not a crash or an arbitrary channel read.
  auto y = in.numberOfChannels() >= 2 ? in.getChannelData(1) : nullptr;
  auto left = out.getChannelData(0);
  auto right = out.getChannelData(1);

  for (int i = 0; i < n; i++) {
    float wy = kAmbisonicReferenceGain * w[i];
    float yv = y ? y[i] : 0.0f;
    left[i] = kBoresightNormalization * (wy - yv);
    right[i] = kBoresightNormalization * (wy + yv);
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

// The MONO counterpart of encodeStereoAsPoints(), for effects whose
// children reduce to MONO now (see reduceForEffect) rather than real
// stereo - a mono signal has no direction to encode, so it's folded into
// W only, at unity (kAmbisonicReferenceGain) - the same "no position set"
// omnidirectional convention computeAmbisonicGains() uses for distance <=
// 0. Added, not overwritten - callers zero `out` first if that's not
// wanted.
inline void encodeMonoAsPoint(const SampleData & mono, SampleData & out) {
  assert(mono.numberOfChannels() == 1);
  assert(out.numberOfChannels() >= 1);

  int n = mono.numberOfFrames();
  auto m = mono.getChannelData(0);
  auto out_w = out.getChannelData(0);

  for (int i = 0; i < n; i++) out_w[i] += kAmbisonicReferenceGain * m[i];
}

// AMBISONIC -> MONO, otherwise unchanged. Used by effects that are
// nonlinear or genuinely need dedicated internal DSP state (Reverb,
// Compressor, Distortion) to request that format from their children, both
// at tree-construction time (Track::getChildChannelConfiguration) and at
// render time (their State classes' render()). There is no real-stereo
// reduction target anymore (ChannelConfiguration::STEREO was removed) - a
// nonlinear effect's children collapse to MONO like everything else, so
// panning doesn't survive underneath one of these three effects; each
// already handles a 1-channel input gracefully (confirmed by reading
// ReverbState/DistortionState/Compressor's own channel loops).
inline ChannelConfiguration reduceForEffect(const ChannelConfiguration & config) {
  if (config.isAmbisonic()) {
    return ChannelConfiguration(config.getAudioOutSampleRate());
  }
  return config;
}

#endif
