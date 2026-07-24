#include "MultiTapDelay.h"
#include "../ParameterSource.h"

#include <cmath>

using namespace std;

namespace {
// Same alternating-sign denormal guard FDNReverb uses in its feedback path
// (bus/FDNReverb.cpp) - keeps a decaying feedback tail's state just above
// the denormal range without being audible.
constexpr float kDenormalGuard = 1e-20f;

// Keeps the feedback loop stable regardless of what a song file's
// delayFeedback value asks for - see setParameters().
constexpr float kMaxFeedbackGain = 0.95f;

// Defaults - shared by the constructor (so a standalone MultiTapDelay
// already sounds sensible before any loadParameters() call) and
// storeParameters()'s deviation-only comparison below, so the two can
// never drift apart into two different notions of "default".
constexpr float kDefaultBaseRows = 0.1875f; // 3/16
constexpr float kDefaultFeedback = 0.5f;
constexpr float kDefaultDamping = 0.3f;
constexpr DelayPattern kDefaultPattern = DelayPattern::Static;
constexpr float kDefaultPatternSpeed = 18.0f;
constexpr float kDefaultWet = 0.354f; // -9dB

// Accepts either a plain decimal ("0.1875") or a fraction ("3/16") - both
// spellings of the same unit (baseRows is a row-count/fraction of a row,
// always - there's no separate ms mode), the fraction form purely for
// hand-edited XML readability. Always written back as a plain decimal
// (see storeParameters()). Same convenience Song.cpp's own (file-local)
// parse_fraction() offered for the legacy flat delayBaseRows attribute
// this type's own <delay> element attribute replaces.
float parse_fraction(const string & text, float default_value) {
  if (text.empty()) return default_value;
  auto slash = text.find('/');
  if (slash == string::npos) return strtof(text.c_str(), nullptr);
  float numerator = strtof(text.substr(0, slash).c_str(), nullptr);
  float denominator = strtof(text.substr(slash + 1).c_str(), nullptr);
  return denominator != 0.0f ? numerator / denominator : default_value;
}

// baseRows/feedback/damping/pattern/patternSpeed/wet for each named preset
// (see MultiTapDelayPreset in MultiTapDelay.h) - used both by
// loadParameters() (as the fallback default for any attribute not
// explicitly present) and storeParameters() (as the deviation-only
// comparison baseline), so a song that just writes `preset="dub"` with no
// further overrides round-trips as exactly that, nothing more. What each
// preset is going for is documented for the user in docs/bus_effects.md,
// not repeated here.
struct PresetValues {
  float baseRows, feedback, damping, patternSpeed, wet;
  DelayPattern pattern;
};

PresetValues presetValues(MultiTapDelayPreset preset) {
  switch (preset) {
  case MultiTapDelayPreset::DEFAULT:
    return { kDefaultBaseRows, kDefaultFeedback, kDefaultDamping, kDefaultPatternSpeed, kDefaultWet, kDefaultPattern };
  case MultiTapDelayPreset::SLAPBACK:
    return { 1.0f / 16.0f, 0.15f, 0.1f, 0.0f, 0.3f, DelayPattern::Static };
  case MultiTapDelayPreset::PINGPONG:
    return { 0.25f, 0.55f, 0.2f, 0.0f, 0.35f, DelayPattern::PingPong };
  case MultiTapDelayPreset::ORBIT:
    return { kDefaultBaseRows, 0.6f, 0.25f, 30.0f, 0.4f, DelayPattern::Orbit };
  case MultiTapDelayPreset::RECEDE:
    return { kDefaultBaseRows, 0.7f, 0.4f, 15.0f, 0.4f, DelayPattern::Recede };
  case MultiTapDelayPreset::DUB:
    return { 0.375f, 0.75f, 0.5f, 0.0f, 0.4f, DelayPattern::Static };
  }
  // Unreachable given the switch above is exhaustive over every defined
  // MultiTapDelayPreset value - see GranularCloud.cpp's presetValues() for
  // the same shape and reasoning.
  return { kDefaultBaseRows, kDefaultFeedback, kDefaultDamping, kDefaultPatternSpeed, kDefaultWet, kDefaultPattern };
}
}

// kDefaultWet is passed to BusEffect's constructor (not applied via a
// post-construction setWetLevel() call) so it also becomes the value
// BusEffect::storeParameters() compares against - see FDNReverb.cpp's
// identical reasoning. Chain-send level is left at BusEffect's own base
// default (kDefaultChainSendLevel) - delay feeding a bit of reverb by
// default is exactly the case that default exists for (BusEffect.h).
MultiTapDelay::MultiTapDelay(int sampleRate) : BusEffect(sampleRate, kDefaultWet) {
  buffer_.assign(static_cast<size_t>(kMaxDelaySeconds * static_cast<float>(sampleRate)) + 1, 0.0f);

  // rowDurationSeconds_'s own member initializer assumes the Song's own
  // default tempo (90 bpm) - setRowDuration() overwrites it with the real
  // tempo-derived value as soon as a song loads.
  setParameters(kDefaultBaseRows, kDefaultFeedback, kDefaultDamping, kDefaultPattern, kDefaultPatternSpeed);
}

void
MultiTapDelay::setParameters(float baseRows, float feedbackGain, float damping, DelayPattern pattern, float patternSpeed) {
  if (baseRows < 0.0f) baseRows = 0.0f;
  if (feedbackGain < 0.0f) feedbackGain = 0.0f;
  if (feedbackGain > kMaxFeedbackGain) feedbackGain = kMaxFeedbackGain;
  if (damping < 0.0f) damping = 0.0f;
  if (damping > 1.0f) damping = 1.0f;

  baseRows_ = baseRows;
  feedbackGain_ = feedbackGain;
  rawDamping_ = damping;
  // Same "never let the one-pole coefficient reach exactly 0" reasoning as
  // FDNReverb::setParameters() - a literal 0 coefficient would freeze the
  // damping filter's state forever, crushing the whole tap, not just the
  // highs.
  dampingCoef_ = 1.0f - damping;
  if (dampingCoef_ < 0.01f) dampingCoef_ = 0.01f;
  pattern_ = pattern;
  patternSpeed_ = patternSpeed;

  recomputeTapLengths();
}

void
MultiTapDelay::setRowDuration(float rowDurationSeconds) {
  rowDurationSeconds_ = rowDurationSeconds;
  recomputeTapLengths();
}

void
MultiTapDelay::recomputeTapLengths() {
  int cap = static_cast<int>(buffer_.size());
  for (int i = 0; i < kNumTaps; i++) {
    float ratio = static_cast<float>(1 << i); // 1x/2x/4x/8x
    float seconds = baseRows_ * ratio * rowDurationSeconds_;
    if (seconds > kMaxDelaySeconds) seconds = kMaxDelaySeconds;
    int len = static_cast<int>(seconds * static_cast<float>(getSampleRate()) + 0.5f);
    if (len < 1) len = 1;
    if (len >= cap) len = cap - 1;
    length_[i] = len;
  }
}

void
MultiTapDelay::advancePass() {
  switch (pattern_) {
  case DelayPattern::Static:
    break;
  case DelayPattern::PingPong:
    azimuth_[kFeedbackTap] = -azimuth_[kFeedbackTap];
    break;
  case DelayPattern::Orbit: {
    float az = fmodf(azimuth_[kFeedbackTap] + patternSpeed_, 360.0f);
    if (az > 180.0f) az -= 360.0f;
    else if (az < -180.0f) az += 360.0f;
    azimuth_[kFeedbackTap] = az;
    break;
  }
  case DelayPattern::Recede: {
    float gainMul = 1.0f - patternSpeed_ / 100.0f;
    if (gainMul < 0.0f) gainMul = 0.0f;
    if (gainMul > 1.0f) gainMul = 1.0f;
    feedbackGainMul_ *= gainMul;

    float elev = elevation_[kFeedbackTap] - patternSpeed_ / 4.0f;
    if (elev < -90.0f) elev = -90.0f;
    if (elev > 90.0f) elev = 90.0f;
    elevation_[kFeedbackTap] = elev;
    break;
  }
  }
}

void
MultiTapDelay::process(const float * input, int frames) {
  for (auto & tap : taps_) {
    if (static_cast<int>(tap.size()) != frames) tap.resize(static_cast<size_t>(frames));
  }

  int bufLen = static_cast<int>(buffer_.size());

  for (int i = 0; i < frames; i++) {
    // Read every tap *before* this sample's write, so they reflect this
    // block's genuine buffer state, not next sample's - same ordering
    // FDNReverb's lines use.
    float tapRaw[kNumTaps];
    for (int t = 0; t < kNumTaps; t++) {
      int readPos = writePos_ - length_[t];
      if (readPos < 0) readPos += bufLen;
      tapRaw[t] = buffer_[static_cast<size_t>(readPos)];
    }

    dampingState_ += dampingCoef_ * (tapRaw[kFeedbackTap] - dampingState_);
    float dampedFeedback = dampingState_ * feedbackGain_;

    for (int t = 0; t < kNumTaps; t++) {
      float gain = kTapGain[t];
      if (t == kFeedbackTap) gain *= feedbackGainMul_;
      taps_[static_cast<size_t>(t)][static_cast<size_t>(i)] = tapRaw[t] * gain;
    }

    float guard = (i % 2 == 0) ? kDenormalGuard : -kDenormalGuard;
    buffer_[static_cast<size_t>(writePos_)] = input[i] + dampedFeedback + guard;
    writePos_++;
    if (writePos_ >= bufLen) writePos_ = 0;

    // One "pass" = one full lap of the feedback tap's own delay length -
    // detected by sample-accurate counting, not a timer, so a pattern
    // update never fires more or less often than the feedback loop itself
    // actually repeats. A `while` (not `if`) so a setParameters() call that
    // shrinks length_[kFeedbackTap] out from under an already-large
    // passCounter_ still catches up correctly instead of needing several
    // more blocks to unwind.
    passCounter_++;
    while (passCounter_ >= length_[kFeedbackTap]) {
      passCounter_ -= length_[kFeedbackTap];
      advancePass();
    }
  }
}

void
MultiTapDelay::loadParameters(const ParameterSource & input) {
  BusEffect::loadParameters(input); // wet/chainSend, generically

  auto preset_text = input.getText("preset");
  if (preset_text == "slapback") preset_ = MultiTapDelayPreset::SLAPBACK;
  else if (preset_text == "pingpong") preset_ = MultiTapDelayPreset::PINGPONG;
  else if (preset_text == "orbit") preset_ = MultiTapDelayPreset::ORBIT;
  else if (preset_text == "recede") preset_ = MultiTapDelayPreset::RECEDE;
  else if (preset_text == "dub") preset_ = MultiTapDelayPreset::DUB;
  // Absent (a bare <delay/>) and the explicit synonym "default" both
  // resolve here - see MultiTapDelayPreset::DEFAULT's own doc comment in
  // MultiTapDelay.h for why writing it back out is still silent either way.
  else preset_ = MultiTapDelayPreset::DEFAULT;

  PresetValues d = presetValues(preset_);

  setParameters(
    parse_fraction(input.getText("baseRows"), d.baseRows),
    input.getFloat("feedback", d.feedback),
    input.getFloat("damping", d.damping),
    parseDelayPattern(input.has("pattern") ? input.getText("pattern") : to_string(d.pattern)),
    input.getFloat("patternSpeed", d.patternSpeed));

  // A preset also implies its own tuned wet level - BusEffect::loadParameters()
  // above already applied "wet", but using this class's flat kDefaultWet as
  // its fallback, which only coincides with d.wet for MultiTapDelayPreset::
  // DEFAULT itself. Redo it here (harmless, exactly a no-op, for DEFAULT) so
  // an explicit wet="..." attribute still wins, but an absent one falls back
  // to the resolved preset's own wet rather than the generic default - same
  // reasoning as GranularCloud::loadParameters().
  setWetLevel(input.getFloat("wet", d.wet));
}

void
MultiTapDelay::storeParameters(ParameterSource & output) const {
  BusEffect::storeParameters(output); // wet/chainSend, generically

  // Deviation-only, unlike effects/Reverb.cpp's own unconditional
  // output.set("preset", to_string(preset_)) (which writes preset="" even
  // for ReverbPreset::NONE) - MultiTapDelayPreset::DEFAULT's to_string() is
  // "" specifically so this stays quiet for it, matching every other
  // implicit-default attribute this class writes below.
  if (preset_ != MultiTapDelayPreset::DEFAULT) output.set("preset", to_string(preset_));

  PresetValues d = presetValues(preset_);
  output.set("baseRows", getBaseRows(), d.baseRows);
  output.set("feedback", getFeedbackGain(), d.feedback);
  output.set("damping", getDamping(), d.damping);
  output.set("pattern", to_string(getPattern()), to_string(d.pattern));
  output.set("patternSpeed", getPatternSpeed(), d.patternSpeed);
}
