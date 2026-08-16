#include "Haze.h"

#include "../ParameterSource.h"
#include "../dsp/NoiseGenerator.h"
#include "../dsp/PinkNoiseFilter.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace std;

namespace {

constexpr float kFilterQ = 0.707f; // Butterworth Q - standard, no reason to expose it
constexpr float kTiltPivotHz = 1000.0f;
constexpr float kDcBlockR = 0.995f; // standard one-pole DC blocker pole - see process()
constexpr float kBiasAmount = 1.5f; // how much DC (in the shaper's own working units) bias=1.0 injects

float dbToLinear(float db) { return powf(10.0f, 0.05f * db); }

// The four waveshaper curves - see plans/drum-bus-saturator.md. `bias`
// (0-1) adds DC into the curve before shaping, which is what generates
// even-order harmonic content (an odd, symmetric curve plus a DC offset
// demodulates into even harmonics - the standard waveshaping-distortion
// mechanism); Haze::process() removes that DC again afterward
// (one-pole DC block) so it doesn't reach the ambisonic bus as a static
// offset - only the even-harmonic *content* the bias generated survives.
float applyShape(float x, SaturatorShape shape, float bias) {
  float biased = x + bias * kBiasAmount;
  switch (shape) {
  case SaturatorShape::Tanh:
    return tanhf(biased);

  case SaturatorShape::Asym: {
    // Differently-shaped positive/negative halves - deliberately stronger
    // even-harmonic generation than plain tanh+bias (see the `body`/
    // `slap` presets, which both lean on this).
    if (biased >= 0.0f) return tanhf(biased * 1.6f) / 1.6f;
    return tanhf(biased * 2.4f) / 1.6f;
  }

  case SaturatorShape::SoftClip: {
    // Same cubic soft-clip shape as effects/Distortion.cpp's SOFT_CLIP
    // case, reused verbatim.
    float y = biased;
    if (y > 1.0f) y = 1.0f;
    else if (y < -1.0f) y = -1.0f;
    y = y - y * y * y / 3.0f;
    y = 1.5f * y - 0.5f * y * y * y;
    return y;
  }

  case SaturatorShape::Fold: {
    // Triangle wavefold: reflect repeatedly into [-1,1] - non-monotonic
    // by construction, which is why `fold` needs 8x oversampling instead
    // of 4x (see the oversampling-factor selection below).
    float y = biased;
    while (y > 1.0f || y < -1.0f) {
      if (y > 1.0f) y = 2.0f - y;
      else if (y < -1.0f) y = -2.0f - y;
    }
    return y;
  }
  }
  return biased;
}

int oversampleStagesForShape(SaturatorShape shape) {
  return shape == SaturatorShape::Fold ? 3 : 2;
}

float fractionFor(HazePreDelayDivision division) {
  switch (division) {
  case HazePreDelayDivision::OneOver256: return 1.0f / 256.0f;
  case HazePreDelayDivision::OneOver128: return 1.0f / 128.0f;
  case HazePreDelayDivision::OneOver64: return 1.0f / 64.0f;
  }
  return 1.0f / 128.0f;
}

// instanceSalt reserved for Haze's own AmbisonicDiffuseEncoder - see that
// class's own doc comment and Haze.h's diffuseEncoder_ member.
constexpr uint32_t kDiffuseEncoderSalt = 0;

// drive/shape/bias/hpf/lpf/tilt/predelay/diffusion for each named preset
// (see HazePreset in Haze.h) - used both by loadParameters() (as the
// fallback default for any attribute not explicitly present) and
// storeParameters() (as the deviation-only comparison baseline), so a
// song that just writes `preset="hash"` with no further overrides
// round-trips as exactly that, nothing more. What each preset is going
// for is documented for the user in docs/bus_effects.md, not repeated
// here - see plans/drum-bus-saturator.md for the full rationale behind
// each preset's numbers.
struct SaturatorPresetValues {
  float driveDb, bias, hpfHz, lpfHz, tiltDb, diffusion;
  SaturatorShape shape;
  HazePreDelayDivision predelay;
};

SaturatorPresetValues presetValues(HazePreset preset) {
  switch (preset) {
  case HazePreset::DEFAULT: // "glue"
    return { 9.0f, 0.3f, 200.0f, 5000.0f, 0.0f, 1.0f, SaturatorShape::Tanh, HazePreDelayDivision::OneOver128 };
  case HazePreset::BODY:
    return { 12.0f, 0.6f, 40.0f, 400.0f, -3.0f, 0.0f, SaturatorShape::Asym, HazePreDelayDivision::OneOver128 };
  case HazePreset::CRUNCH:
    return { 20.0f, 0.2f, 150.0f, 7000.0f, 2.0f, 0.85f, SaturatorShape::SoftClip, HazePreDelayDivision::OneOver128 };
  case HazePreset::SLAP:
    return { 15.0f, 0.5f, 300.0f, 3500.0f, -4.0f, 1.0f, SaturatorShape::Asym, HazePreDelayDivision::OneOver64 };
  case HazePreset::HASH:
    return { 30.0f, 0.1f, 400.0f, 2500.0f, 0.0f, 1.0f, SaturatorShape::Fold, HazePreDelayDivision::OneOver256 };
  case HazePreset::AIR:
    return { 8.0f, 0.0f, 4000.0f, 14000.0f, 5.0f, 1.0f, SaturatorShape::Tanh, HazePreDelayDivision::OneOver256 };
  }
  // Unreachable given the switch above is exhaustive over every defined
  // HazePreset value - see MultiTapDelay.cpp's presetValues() for
  // the same shape and reasoning.
  return { 9.0f, 0.3f, 200.0f, 5000.0f, 0.0f, 1.0f, SaturatorShape::Tanh, HazePreDelayDivision::OneOver128 };
}

// Static auto-gain compensation: a small drive x bias grid, per shape,
// each node's compensation gain precomputed once (lazily, on first use,
// shared process-wide - the transfer function depends only on shape/
// drive/bias, never on any instance state) by running a fixed reference
// signal through the exact same shape/bias transfer function and
// measuring how far its RMS moved from the reference's own (undriven)
// RMS - see plans/drum-bus-saturator.md's "static curve integral, not a
// running RMS" design decision. Deliberately computed WITHOUT
// oversampling: this is measuring the memoryless nonlinearity's own
// average RMS transfer ratio for a fixed statistical input, which
// oversampling (an anti-aliasing concern, not a level concern) doesn't
// materially change.
constexpr int kDriveSteps = 10;  // 0, 4, ..., 36 dB
constexpr int kBiasSteps = 11;   // 0.0, 0.1, ..., 1.0
constexpr float kDriveStepDb = 4.0f;
constexpr float kBiasStep = 0.1f;
constexpr int kReferenceSamples = 4096;

using GainTable = array<array<float, kBiasSteps>, kDriveSteps>;

const GainTable & compensationTable(SaturatorShape shape) {
  static array<GainTable, 4> tables{};
  static array<bool, 4> computed{ false, false, false, false };

  size_t idx = static_cast<size_t>(shape);
  if (computed[idx]) return tables[idx];

  array<float, kReferenceSamples> reference{};
  NoiseGenerator noise(0xD5A7c0deu);
  PinkNoiseFilter pink;
  for (auto & s : reference) s = pink.process(noise.next());

  double refEnergy = 0.0;
  for (float s : reference) refEnergy += static_cast<double>(s) * s;
  float refRms = static_cast<float>(sqrt(refEnergy / kReferenceSamples));

  auto & table = tables[idx];
  array<float, kReferenceSamples> shaped{};
  for (int d = 0; d < kDriveSteps; d++) {
    float driveLinear = dbToLinear(static_cast<float>(d) * kDriveStepDb);
    for (int b = 0; b < kBiasSteps; b++) {
      float bias = static_cast<float>(b) * kBiasStep;
      // AC-only RMS (mean removed), not raw RMS: `bias` injects a real DC
      // offset into the shaper's output (that's the whole mechanism by
      // which it generates even-order content - see applyShape()'s own
      // comment), but process()'s DC blocker removes that offset again
      // before the signal ever reaches the bus. Measuring raw (DC-
      // including) RMS here would calibrate against a quantity the real
      // chain never actually delivers - dominated by the fixed DC term
      // rather than the driven AC content, especially at low drive, where
      // the DC term is large relative to the still-quiet signal - which
      // badly under-compensates once that DC is stripped downstream.
      double mean = 0.0;
      for (size_t i = 0; i < kReferenceSamples; i++) {
        float y = applyShape(reference[i] * driveLinear, shape, bias);
        shaped[i] = y;
        mean += y;
      }
      mean /= kReferenceSamples;
      double outEnergy = 0.0;
      for (float y : shaped) outEnergy += (static_cast<double>(y) - mean) * (static_cast<double>(y) - mean);
      float outRms = static_cast<float>(sqrt(outEnergy / kReferenceSamples));
      table[static_cast<size_t>(d)][static_cast<size_t>(b)] = outRms > 1e-6f ? refRms / outRms : 1.0f;
    }
  }
  computed[idx] = true;
  return table;
}

float interpolateCompensation(SaturatorShape shape, float driveDb, float bias) {
  const auto & table = compensationTable(shape);

  float df = driveDb / kDriveStepDb;
  if (df < 0.0f) df = 0.0f;
  if (df > kDriveSteps - 1) df = static_cast<float>(kDriveSteps - 1);
  int d0 = static_cast<int>(df);
  int d1 = min(d0 + 1, kDriveSteps - 1);
  float dt = df - static_cast<float>(d0);

  float bf = bias / kBiasStep;
  if (bf < 0.0f) bf = 0.0f;
  if (bf > kBiasSteps - 1) bf = static_cast<float>(kBiasSteps - 1);
  int b0 = static_cast<int>(bf);
  int b1 = min(b0 + 1, kBiasSteps - 1);
  float bt = bf - static_cast<float>(b0);

  float v00 = table[static_cast<size_t>(d0)][static_cast<size_t>(b0)];
  float v01 = table[static_cast<size_t>(d0)][static_cast<size_t>(b1)];
  float v10 = table[static_cast<size_t>(d1)][static_cast<size_t>(b0)];
  float v11 = table[static_cast<size_t>(d1)][static_cast<size_t>(b1)];
  float v0 = v00 + (v01 - v00) * bt;
  float v1 = v10 + (v11 - v10) * bt;
  return v0 + (v1 - v0) * dt;
}

}

Haze::Haze(int sampleRate)
  : BusEffect(sampleRate),
    hpf_(FilterType::highpass), lpf_(FilterType::lowpass),
    tiltLow_(FilterType::lowshelf), tiltHigh_(FilterType::highshelf),
    diffuseEncoder_(sampleRate, kDiffuseEncoderSalt) {
  predelayBuffer_.assign(static_cast<size_t>(kMaxPredelaySeconds * static_cast<float>(sampleRate)) + 1, 0.0f);
  auto d = presetValues(HazePreset::DEFAULT);
  setParameters(d.driveDb, d.shape, d.bias, d.hpfHz, d.lpfHz, d.tiltDb, 0.0f, d.predelay, d.diffusion);
}

void
Haze::setParameters(float driveDb, SaturatorShape shape, float bias, float hpfHz, float lpfHz, float tiltDb, float trimDb, HazePreDelayDivision predelay, float diffusion) {
  shape_ = shape;
  bias_ = bias < 0.0f ? 0.0f : (bias > 1.0f ? 1.0f : bias);
  driveLinear_ = dbToLinear(driveDb);
  oversampleStages_ = oversampleStagesForShape(shape_);
  rawDriveDb_ = driveDb;

  float sr = static_cast<float>(getSampleRate());
  if (hpfHz < 1.0f) hpfHz = 1.0f;
  if (lpfHz > sr * 0.49f) lpfHz = sr * 0.49f;
  hpf_.set(hpfHz / sr, kFilterQ);
  lpf_.set(lpfHz / sr, kFilterQ);
  rawHpfHz_ = hpfHz;
  rawLpfHz_ = lpfHz;

  tiltLow_.set(kTiltPivotHz / sr, kFilterQ, -tiltDb * 0.5f);
  tiltHigh_.set(kTiltPivotHz / sr, kFilterQ, tiltDb * 0.5f);
  rawTiltDb_ = tiltDb;

  autoGainCompensation_ = interpolateCompensation(shape_, driveDb, bias_);
  trimLinear_ = dbToLinear(trimDb);
  rawTrimDb_ = trimDb;

  diffusion_ = diffusion < 0.0f ? 0.0f : (diffusion > 1.0f ? 1.0f : diffusion);

  predelayDivision_ = predelay;
  recomputePredelaySamples();

  // Warm up the whole downstream chain (oversampler/decimator FIR
  // history, DC blocker) against silence before any real audio reaches
  // it. Whenever bias > 0, literal silence still resolves to a real,
  // nonzero steady-state DC term inside the shaper (see applyShape()'s
  // own comment) - every piece of state between the shaper and the
  // output (the halfband chain's own zero-initial history, the DC
  // blocker) starts cold and has to settle into that new steady state,
  // and letting that settling happen against real audio produces a
  // genuine, audible broadband transient right at the start of playback
  // (confirmed by direct measurement: silence in, ~0.2 amplitude two
  // samples later, with the default "glue" preset - an analytically
  // pre-seeded DC-blocker state alone isn't enough, since the halfband
  // chain's own settling reaches the blocker gradually, not as an
  // already-converged constant). kWarmUpFrames comfortably exceeds both
  // the halfband chain's own settle time and the longest possible
  // pre-delay, so the pre-delay line's ring buffer has fully cycled past
  // whatever transient this writes into it too. Harmless to repeat on
  // every setParameters() call (including a live preset switch, not just
  // construction) - this class already snaps every other coefficient
  // instantly with no smoothing, so a settle-again moment here is no new
  // kind of artifact, and avoiding it would need tracking "is this really
  // the first call" state for no real benefit.
  constexpr int kWarmUpFrames = 4096;
  std::vector<float> silence(static_cast<size_t>(kWarmUpFrames), 0.0f);
  process(silence.data(), kWarmUpFrames);
}

void
Haze::setRowDuration(float rowDurationSeconds) {
  rowDurationSeconds_ = rowDurationSeconds;
  recomputePredelaySamples();
}

void
Haze::recomputePredelaySamples() {
  // predelayDivision_'s fraction is of a WHOLE NOTE, not of a row - a row
  // is already a short 16th-note-ish subdivision (getRowDuration() =
  // 60/(4*tempo), i.e. 4 rows/beat, 4 beats/whole note = 16 rows/whole
  // note), so "1/64" as a fraction of a row would be a sub-2ms sliver at
  // any working tempo, nowhere near the 5-35ms window this effect is
  // designed around. As a fraction of a whole note, "1/64" reads the same
  // way a delay pedal's own tempo-synced note-division control would
  // (1/64-note, 1/128-note, ...), which is what actually lands in that
  // window - see plans/drum-bus-saturator.md.
  constexpr float kRowsPerWholeNote = 16.0f;
  float seconds = fractionFor(predelayDivision_) * kRowsPerWholeNote * rowDurationSeconds_;
  if (seconds < kMinPredelaySeconds) seconds = kMinPredelaySeconds;
  if (seconds > kMaxPredelaySeconds) seconds = kMaxPredelaySeconds;

  int samples = static_cast<int>(seconds * static_cast<float>(getSampleRate()) + 0.5f);
  int cap = static_cast<int>(predelayBuffer_.size());
  if (samples < 1) samples = 1;
  if (samples >= cap) samples = cap - 1;
  predelaySamples_ = samples;
}

void
Haze::process(const float * input, int frames) {
  if (static_cast<int>(bandpassed_.size()) != frames) bandpassed_.resize(static_cast<size_t>(frames));
  if (static_cast<int>(coreOutput_.size()) != frames) coreOutput_.resize(static_cast<size_t>(frames));
  for (int s = 0; s < kMaxOversampleStages; s++) {
    size_t size = static_cast<size_t>(frames) << (s + 1); // 2x, 4x, 8x
    if (oversampled_[static_cast<size_t>(s)].size() != size) oversampled_[static_cast<size_t>(s)].resize(size);
  }

  for (int i = 0; i < frames; i++) bandpassed_[static_cast<size_t>(i)] = input[i] * driveLinear_;
  hpf_.apply(static_cast<size_t>(frames), bandpassed_.data());
  lpf_.apply(static_cast<size_t>(frames), bandpassed_.data());

  // Oversample.
  const float * upIn = bandpassed_.data();
  int upFrames = frames;
  for (int s = 0; s < oversampleStages_; s++) {
    upStages_[static_cast<size_t>(s)].upsample(upIn, upFrames, oversampled_[static_cast<size_t>(s)].data());
    upIn = oversampled_[static_cast<size_t>(s)].data();
    upFrames *= 2;
  }

  // Waveshape in place on the final (most-oversampled) buffer.
  float * shaped = oversampled_[static_cast<size_t>(oversampleStages_ - 1)].data();
  for (int i = 0; i < upFrames; i++) shaped[i] = applyShape(shaped[i], shape_, bias_);

  // Decimate back down - stage s reads from the current rate and writes
  // into either the next-lower oversample buffer or, at s==0, back into
  // bandpassed_ (safe to reuse: its original pre-oversample content was
  // already fully consumed by the upsample loop above, before any
  // decimate stage runs).
  const float * downIn = shaped;
  int downFrames = upFrames;
  for (int s = oversampleStages_ - 1; s >= 0; s--) {
    int outFrames = downFrames / 2;
    float * out = (s == 0) ? bandpassed_.data() : oversampled_[static_cast<size_t>(s - 1)].data();
    downStages_[static_cast<size_t>(s)].downsample(downIn, outFrames, out);
    downIn = out;
    downFrames = outFrames;
  }

  for (int i = 0; i < frames; i++) {
    float x = bandpassed_[static_cast<size_t>(i)];
    float blocked = x - dcBlockX1_ + kDcBlockR * dcBlockY1_;
    // Snap to exact 0 once decay has brought it this close - a one-pole
    // decay only asymptotes toward 0 and, in float32, can end up pinned
    // at some tiny nonzero value forever (the same real, confirmed bug
    // DiracAnalyzer.cpp's own grid smoothing already documents and
    // guards against - see that file for the fuller explanation).
    // Without this, whenever bias > 0 a silent (or momentarily silent)
    // input leaves this stuck-forever residual reaching the ambisonic
    // bus at every sample for the rest of the song - inaudible on its
    // own, but a genuine, persistent, spuriously "diffuse" signal a
    // directional analyzer like DiracAnalyzer picks up as real content
    // with no actual source. 1e-4f, not DiracAnalyzer's own 1e-6f: this
    // recursion's actual stuck point, measured directly against this
    // effect's real oversample/shape/decimate chain (not an isolated
    // model of it), sits close enough to 1e-6 that DiracAnalyzer's own
    // threshold sometimes misses it depending on unrelated codegen
    // details (build flags, surrounding code) that shift exactly where
    // float32 rounding stalls the decay - a threshold two full orders of
    // magnitude higher was confirmed (by direct, repeated measurement)
    // to reliably clear it regardless, while still being far below any
    // audible or analytically meaningful level.
    if (fabsf(blocked) < 1e-4f) blocked = 0.0f;
    dcBlockX1_ = x;
    dcBlockY1_ = blocked;
    coreOutput_[static_cast<size_t>(i)] = blocked;
  }

  tiltLow_.apply(static_cast<size_t>(frames), coreOutput_.data());
  tiltHigh_.apply(static_cast<size_t>(frames), coreOutput_.data());

  float postGain = autoGainCompensation_ * trimLinear_;
  for (int i = 0; i < frames; i++) coreOutput_[static_cast<size_t>(i)] *= postGain;

  // Tempo-synced pre-delay: reads coreOutput_ back predelaySamples_ ago
  // through a single delay line - same read-before-write ordering
  // FDNReverb's/MultiTapDelay's own delay lines use, so this block's taps
  // reflect the buffer's genuine prior state, not this block's own writes.
  if (static_cast<int>(predelayed_.size()) != frames) predelayed_.resize(static_cast<size_t>(frames));
  int bufLen = static_cast<int>(predelayBuffer_.size());
  for (int i = 0; i < frames; i++) {
    int readPos = predelayWritePos_ - predelaySamples_;
    if (readPos < 0) readPos += bufLen;
    predelayed_[static_cast<size_t>(i)] = predelayBuffer_[static_cast<size_t>(readPos)];
    predelayBuffer_[static_cast<size_t>(predelayWritePos_)] = coreOutput_[static_cast<size_t>(i)];
    predelayWritePos_++;
    if (predelayWritePos_ >= bufLen) predelayWritePos_ = 0;
  }
}

void
Haze::getChainSendSum(float * out, int frames) const {
  int n = min(frames, static_cast<int>(coreOutput_.size()));
  for (int i = 0; i < n; i++) out[i] = coreOutput_[static_cast<size_t>(i)];
  for (int i = n; i < frames; i++) out[i] = 0.0f;
}

void
Haze::getPredelayedMono(float * out, int frames) const {
  int n = min(frames, static_cast<int>(predelayed_.size()));
  for (int i = 0; i < n; i++) out[i] = predelayed_[static_cast<size_t>(i)];
  for (int i = n; i < frames; i++) out[i] = 0.0f;
}

void
Haze::encodeDirect(AudioBuffer & busAmbisonic, int frames) {
  if (static_cast<int>(predelayed_.size()) != frames) return; // process() hasn't run yet for this frame count
  diffuseEncoder_.encode(busAmbisonic, predelayed_.data(), frames, diffusion_, getWetLevel());
}

void
Haze::loadParameters(const ParameterSource & input) {
  BusEffect::loadParameters(input); // wet/chainSend, generically

  auto preset_text = input.getText("preset");
  if (preset_text == "body") preset_ = HazePreset::BODY;
  else if (preset_text == "crunch") preset_ = HazePreset::CRUNCH;
  else if (preset_text == "slap") preset_ = HazePreset::SLAP;
  else if (preset_text == "hash") preset_ = HazePreset::HASH;
  else if (preset_text == "air") preset_ = HazePreset::AIR;
  // Absent (a bare <haze/>) and the explicit synonym "default" both
  // resolve here - see HazePreset::DEFAULT's own doc comment in Haze.h
  // for why writing it back out is still silent either way.
  else preset_ = HazePreset::DEFAULT;

  auto d = presetValues(preset_);

  setParameters(
    input.getFloat("drive", d.driveDb),
    parseSaturatorShape(input.has("shape") ? input.getText("shape") : to_string(d.shape)),
    input.getFloat("bias", d.bias),
    input.getFloat("hpf", d.hpfHz),
    input.getFloat("lpf", d.lpfHz),
    input.getFloat("tilt", d.tiltDb),
    input.getFloat("trim", 0.0f),
    parseHazePreDelayDivision(input.has("predelay") ? input.getText("predelay") : to_string(d.predelay)),
    input.getFloat("diffusion", d.diffusion));
}

void
Haze::storeParameters(ParameterSource & output) const {
  BusEffect::storeParameters(output); // wet/chainSend, generically

  // Deviation-only - HazePreset::DEFAULT's to_string() is "" specifically
  // so this stays quiet for it, matching every other implicit-default
  // attribute this class writes below.
  if (preset_ != HazePreset::DEFAULT) output.set("preset", to_string(preset_));

  auto d = presetValues(preset_);
  output.set("drive", getDriveDb(), d.driveDb);
  output.set("shape", to_string(getShape()), to_string(d.shape));
  output.set("bias", getBias(), d.bias);
  output.set("hpf", getHpf(), d.hpfHz);
  output.set("lpf", getLpf(), d.lpfHz);
  output.set("tilt", getTilt(), d.tiltDb);
  // trim has no per-preset value - it's always a flat 0dB manual offset
  // on top of whatever the preset/other attributes already resolve to.
  output.set("trim", getTrim(), 0.0f);
  output.set("predelay", to_string(getPredelayDivision()), to_string(d.predelay));
  output.set("diffusion", getDiffusion(), d.diffusion);
}
