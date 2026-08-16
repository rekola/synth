#include "FDNReverb.h"
#include "../state/ParameterSource.h"

#include <cassert>
#include <cmath>

using namespace std;

namespace {

// Medium-hall defaults - shared by the constructor (so a standalone
// FDNReverb already sounds sensible before any loadParameters() call) and
// storeParameters()'s deviation-only comparison below, so the two can
// never drift apart into two different notions of "default".
constexpr float kDefaultSize = 1.0f;
constexpr float kDefaultDecay = 1.8f;
constexpr float kDefaultDamping = 0.1f;
constexpr float kDefaultPreDelay = 0.02f;
constexpr float kDefaultWet = 0.2512f; // -12dB

// Base delay lengths at size=1.0, in milliseconds - chosen as prime
// values spread across the target ~20-90ms range so no two lines share a
// small common factor, which is what actually avoids audible periodicity/
// flutter in the tail (confirmed by inspecting a rendered impulse
// response - see FDNReverbTests.cpp / the manual render+analyze
// verification in the plan this was built from). Millisecond values
// (rather than a fixed sample count at some reference rate) so the
// spread scales correctly to whatever sample rate is actually configured
// (--samplerate N), not just a hardcoded reference rate.
constexpr float kBaseDelayMs[FDNReverb::kNumLines] = { 23.0f, 29.0f, 37.0f, 43.0f, 53.0f, 61.0f, 71.0f, 83.0f };

// Widest `size` this class supports - buffers are allocated once, at
// construction, for this multiplier, so setParameters() never reallocates.
constexpr float kMaxSize = 3.0f;
constexpr float kMinSize = 0.1f;
constexpr float kMaxPreDelaySeconds = 0.2f;
constexpr float kMinDecaySeconds = 0.01f; // avoids a division by zero below

// Alternating sign, spreads the input across all 8 lines so the tail's
// onset isn't a single coherent click replayed 8 times. 1/sqrt(8) keeps
// the total injected energy comparable to a single unscaled input.
constexpr float kInjectionGain = 0.35355339059327373f;

// Short input-diffusion allpasses (not size-dependent) - a handful of ms,
// increases early echo density before the signal reaches the network.
constexpr float kDiffuserMs[3] = { 3.0f, 5.0f, 7.0f };
constexpr float kDiffuserGain = 0.5f;

// Denormal guard: decaying feedback filters are a classic denormal CPU
// trap. A tiny alternating-sign value keeps every line's state just above
// the denormal range without being audible - unlike FTZ/DAZ, this doesn't
// change floating-point behavior for any other DSP sharing this audio
// thread (this codebase doesn't set FTZ/DAZ anywhere).
constexpr float kDenormalGuard = 1e-20f;

// How long a `size` change's tap-output fade takes - see FDNReverb.h.
constexpr float kSizeChangeFadeMs = 10.0f;

// size/decay/damping/preDelay/wet for each named preset (see
// FDNReverbPreset in FDNReverb.h) - used both by loadParameters() (as the
// fallback default for any attribute not explicitly present) and
// storeParameters() (as the deviation-only comparison baseline), so a song
// that just writes `preset="hall"` with no further overrides round-trips
// as exactly that, nothing more. What each preset is going for is
// documented for the user in docs/bus_effects.md, not repeated here.
struct PresetValues {
  float size, decay, damping, preDelay, wet;
};

PresetValues presetValues(FDNReverbPreset preset) {
  switch (preset) {
  case FDNReverbPreset::DEFAULT:
    return { kDefaultSize, kDefaultDecay, kDefaultDamping, kDefaultPreDelay, kDefaultWet };
  case FDNReverbPreset::ROOM:
    return { 0.3f, 0.6f, 0.35f, 0.005f, 0.18f };
  case FDNReverbPreset::HALL:
    return { 2.0f, 3.2f, 0.15f, 0.03f, 0.3f };
  case FDNReverbPreset::CATHEDRAL:
    return { 3.0f, 6.5f, 0.4f, 0.06f, 0.38f };
  case FDNReverbPreset::PLATE:
    return { 0.4f, 1.4f, 0.02f, 0.0f, 0.28f };
  case FDNReverbPreset::AMBIENT:
    return { 2.2f, 9.0f, 0.65f, 0.09f, 0.42f };
  }
  // Unreachable given the switch above is exhaustive over every defined
  // FDNReverbPreset value - see GranularCloud.cpp's presetValues() for the
  // same shape and reasoning.
  return { kDefaultSize, kDefaultDecay, kDefaultDamping, kDefaultPreDelay, kDefaultWet };
}
}

// kDefaultWet is passed to BusEffect's constructor (not applied via a
// post-construction setWetLevel() call) so it also becomes the value
// BusEffect::storeParameters() compares against - a single source of
// truth for "reverb's own tuned wet default" shared by construction and
// deviation-only saving alike. chainSendLevel is left at BusEffect's own
// base default (kDefaultChainSendLevel) - inert here anyway, since slot A
// is always the chain's terminal.
FDNReverb::FDNReverb(int sampleRate) : BusEffect(sampleRate, kDefaultWet) {
  predelayBuffer_.assign(static_cast<size_t>(kMaxPreDelaySeconds * static_cast<float>(sampleRate)) + 1, 0.0f);

  for (size_t i = 0; i < diffusers_.size(); i++) {
    int len = static_cast<int>(kDiffuserMs[i] * 0.001f * static_cast<float>(sampleRate)) + 1;
    diffusers_[i].buffer.assign(static_cast<size_t>(len), 0.0f);
    diffusers_[i].gain = kDiffuserGain;
  }

  for (int i = 0; i < kNumLines; i++) {
    int maxLen = static_cast<int>(kBaseDelayMs[static_cast<size_t>(i)] * kMaxSize * 0.001f * static_cast<float>(sampleRate)) + 1;
    lines_[static_cast<size_t>(i)].buffer.assign(static_cast<size_t>(maxLen), 0.0f);
  }

  // Medium-hall defaults: RT60 1.8s, pre-delay 20ms, damping tuned so
  // high frequencies decay roughly twice as fast as the broadband tail
  // (see the derivation in the damping parameter's own doc comment in
  // FDNReverb.h).
  setParameters(kDefaultSize, kDefaultDecay, kDefaultDamping, kDefaultPreDelay);
}

SphericalPosition
FDNReverb::getTapDirection(int i) const {
  static const auto directions = cubeVertexDirections();
  auto & d = directions[static_cast<size_t>(i)];
  return SphericalPosition{ d.azimuth, d.elevation, 1.0f };
}

void
FDNReverb::setParameters(float size, float decayRT60Seconds, float damping, float preDelaySeconds) {
  if (size < kMinSize) size = kMinSize;
  if (size > kMaxSize) size = kMaxSize;
  if (decayRT60Seconds < kMinDecaySeconds) decayRT60Seconds = kMinDecaySeconds;
  if (damping < 0.0f) damping = 0.0f;
  if (damping > 1.0f) damping = 1.0f;
  if (preDelaySeconds < 0.0f) preDelaySeconds = 0.0f;
  if (preDelaySeconds > kMaxPreDelaySeconds) preDelaySeconds = kMaxPreDelaySeconds;

  rawDecay_ = decayRT60Seconds;
  rawDamping_ = damping;
  rawPreDelay_ = preDelaySeconds;

  if (fabsf(size - currentSize_) > 0.0001f) {
    fadeTotal_ = fadeRemaining_ = static_cast<int>(kSizeChangeFadeMs * 0.001f * static_cast<float>(getSampleRate())) + 1;
    currentSize_ = size;
  }

  // Distinct integer lengths, re-derived every call (cheap - 8 lines) so
  // this always reflects the live `size` without reallocating: buffers
  // were sized at construction for the worst case (kMaxSize).
  int usedLengths[kNumLines];
  for (int i = 0; i < kNumLines; i++) {
    int len = static_cast<int>(kBaseDelayMs[static_cast<size_t>(i)] * size * 0.001f * static_cast<float>(getSampleRate()) + 0.5f);
    if (len < 1) len = 1;
    // Distinctness pass: nudge forward past any earlier line's length
    // already chosen this call - keeps lines from ever colliding into the
    // same delay after rounding, without needing them to stay prime.
    for (int j = 0; j < i; j++) {
      if (usedLengths[j] == len) len++;
    }
    usedLengths[i] = len;

    auto & line = lines_[static_cast<size_t>(i)];
    int cap = static_cast<int>(line.buffer.size());
    line.length = len < cap ? len : cap - 1;

    // gain_i = 10^(-3 * delay_i / (RT60 * fs))
    float delaySeconds = static_cast<float>(line.length) / static_cast<float>(getSampleRate());
    line.feedbackGain = powf(10.0f, -3.0f * delaySeconds / decayRT60Seconds);
    assert(line.feedbackGain < 1.0f); // must hold for any finite RT60 > 0 - see setParameters()'s clamp above
  }

  // The one-pole filter's actual coefficient is the inverse of the
  // user-facing damping amount: damping=0 ("no damping, brightest") must
  // give a coefficient of 1 (the filter tracks its input exactly, no
  // filtering at all - see the derivation in FDNReverb.h), damping=1
  // ("heavily damped, darkest") approaches a coefficient of 0. Never let
  // it reach exactly 0: at coefficient 0 the filter's state never updates
  // from its initial value again (a literal division-by-zero in the
  // filter's effective time constant), silently crushing the entire
  // signal - not just the highs a damping filter is meant to affect - to
  // whatever the state was last frozen at (0, at startup). Clamping to a
  // small nonzero floor keeps it a heavy-but-real low-pass instead.
  dampingCoef_ = 1.0f - damping;
  if (dampingCoef_ < 0.01f) dampingCoef_ = 0.01f;
  predelayLength_ = static_cast<int>(preDelaySeconds * static_cast<float>(getSampleRate()));
  int predelayCap = static_cast<int>(predelayBuffer_.size());
  if (predelayLength_ >= predelayCap) predelayLength_ = predelayCap - 1;
}

void
FDNReverb::process(const float * input, int frames) {
  for (auto & tap : taps_) {
    if (static_cast<int>(tap.size()) != frames) tap.resize(static_cast<size_t>(frames));
  }

  int predelayBufLen = static_cast<int>(predelayBuffer_.size());

  for (int i = 0; i < frames; i++) {
    // Pre-delay.
    int predelayReadPos = predelayWritePos_ - predelayLength_;
    if (predelayReadPos < 0) predelayReadPos += predelayBufLen;
    float predelayed = predelayBuffer_[static_cast<size_t>(predelayReadPos)];
    predelayBuffer_[static_cast<size_t>(predelayWritePos_)] = input[i];
    predelayWritePos_++;
    if (predelayWritePos_ >= predelayBufLen) predelayWritePos_ = 0;

    // Input diffusion: cascaded Schroeder allpasses.
    float diffused = predelayed;
    for (auto & ap : diffusers_) {
      int bufLen = static_cast<int>(ap.buffer.size());
      float delayed = ap.buffer[static_cast<size_t>(ap.pos)];
      float v = diffused - ap.gain * delayed;
      ap.buffer[static_cast<size_t>(ap.pos)] = v;
      diffused = delayed + ap.gain * v;
      ap.pos++;
      if (ap.pos >= bufLen) ap.pos = 0;
    }

    // Read each line's current output - this sample's 8 taps - *before*
    // the feedback path below processes them, so the taps reflect this
    // block's genuine line state, not next block's.
    float lineOut[kNumLines];
    for (int l = 0; l < kNumLines; l++) {
      auto & line = lines_[static_cast<size_t>(l)];
      int bufLen = static_cast<int>(line.buffer.size());
      int readPos = line.writePos - line.length;
      if (readPos < 0) readPos += bufLen;
      lineOut[l] = line.buffer[static_cast<size_t>(readPos)];
    }

    // Fade only the tap output actually returned to the caller, not the
    // feedback path itself - the network keeps running at full level
    // underneath (no energy/state lost), only the audible jump right at
    // a `size` change is masked briefly.
    float fadeGain = 1.0f;
    if (fadeRemaining_ > 0) {
      fadeGain = static_cast<float>(fadeTotal_ - fadeRemaining_) / static_cast<float>(fadeTotal_);
      fadeRemaining_--;
    }
    for (int l = 0; l < kNumLines; l++) taps_[static_cast<size_t>(l)][static_cast<size_t>(i)] = lineOut[l] * fadeGain;

    // Damping (one-pole) + RT60-derived feedback gain, per line.
    float damped[kNumLines];
    for (int l = 0; l < kNumLines; l++) {
      auto & line = lines_[static_cast<size_t>(l)];
      line.dampingState += dampingCoef_ * (lineOut[l] - line.dampingState);
      damped[l] = line.dampingState * line.feedbackGain;
    }

    // Lossless Householder feedback matrix, O(N) reflection form (not a
    // dense NxN multiply): y = x - (2/N) * sum(x) * ones.
    float sum = 0.0f;
    for (int l = 0; l < kNumLines; l++) sum += damped[l];
    float scale = 2.0f / static_cast<float>(kNumLines);
    float mixed[kNumLines];
    for (int l = 0; l < kNumLines; l++) mixed[l] = damped[l] - scale * sum;

    // Inject (alternating sign, to decorrelate onset) + denormal guard,
    // write back for next sample.
    for (int l = 0; l < kNumLines; l++) {
      auto & line = lines_[static_cast<size_t>(l)];
      float sign = (l % 2 == 0) ? 1.0f : -1.0f;
      float guard = ((i + l) % 2 == 0) ? kDenormalGuard : -kDenormalGuard;
      int bufLen = static_cast<int>(line.buffer.size());
      line.buffer[static_cast<size_t>(line.writePos)] = mixed[l] + sign * kInjectionGain * diffused + guard;
      line.writePos++;
      if (line.writePos >= bufLen) line.writePos = 0;
    }
  }
}

void
FDNReverb::loadParameters(const ParameterSource & input) {
  BusEffect::loadParameters(input); // wet/chainSend, generically

  auto preset_text = input.getText("preset");
  if (preset_text == "room") preset_ = FDNReverbPreset::ROOM;
  else if (preset_text == "hall") preset_ = FDNReverbPreset::HALL;
  else if (preset_text == "cathedral") preset_ = FDNReverbPreset::CATHEDRAL;
  else if (preset_text == "plate") preset_ = FDNReverbPreset::PLATE;
  else if (preset_text == "ambient") preset_ = FDNReverbPreset::AMBIENT;
  // Absent (a bare <reverb/>) and the explicit synonym "default" both
  // resolve here - see FDNReverbPreset::DEFAULT's own doc comment in
  // FDNReverb.h for why writing it back out is still silent either way.
  else preset_ = FDNReverbPreset::DEFAULT;

  PresetValues d = presetValues(preset_);

  setParameters(
    input.getFloat("size", d.size),
    input.getFloat("decay", d.decay),
    input.getFloat("damping", d.damping),
    input.getFloat("preDelay", d.preDelay));

  // A preset also implies its own tuned wet level - BusEffect::loadParameters()
  // above already applied "wet", but using this class's flat kDefaultWet as
  // its fallback, which only coincides with d.wet for FDNReverbPreset::
  // DEFAULT itself. Redo it here (harmless, exactly a no-op, for DEFAULT) so
  // an explicit wet="..." attribute still wins, but an absent one falls back
  // to the resolved preset's own wet rather than the generic default - same
  // reasoning as GranularCloud::loadParameters().
  setWetLevel(input.getFloat("wet", d.wet));
}

void
FDNReverb::storeParameters(ParameterSource & output) const {
  BusEffect::storeParameters(output); // wet/chainSend, generically

  // Deviation-only - FDNReverbPreset::DEFAULT's to_string() is ""
  // specifically so this stays quiet for it, matching every other
  // implicit-default attribute this class writes below.
  if (preset_ != FDNReverbPreset::DEFAULT) output.set("preset", to_string(preset_));

  PresetValues d = presetValues(preset_);
  output.set("size", getSize(), d.size);
  output.set("decay", getDecay(), d.decay);
  output.set("damping", getDamping(), d.damping);
  output.set("preDelay", getPreDelay(), d.preDelay);
}
