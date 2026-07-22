#include "MultiTapDelay.h"

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
}

MultiTapDelay::MultiTapDelay(int sampleRate) : BusEffect(sampleRate) {
  buffer_.assign(static_cast<size_t>(kMaxDelaySeconds * static_cast<float>(sampleRate)) + 1, 0.0f);

  // Matches Song.h's delay* defaults exactly, so a MultiTapDelay
  // constructed standalone (e.g. in a test, before any real setParameters()
  // call) already behaves sensibly. rowDurationSeconds here assumes the
  // Song's own default tempo (90 bpm) - SendBusProcessor overwrites this
  // with the real tempo-derived value as soon as a song loads.
  setParameters(0.1875f, 0.5f, 0.3f, DelayPattern::Static, 18.0f, 60.0f / 4.0f / 90.0f);
}

void
MultiTapDelay::setParameters(float baseRows, float feedbackGain, float damping, DelayPattern pattern, float patternSpeed, float rowDurationSeconds) {
  if (baseRows < 0.0f) baseRows = 0.0f;
  if (feedbackGain < 0.0f) feedbackGain = 0.0f;
  if (feedbackGain > kMaxFeedbackGain) feedbackGain = kMaxFeedbackGain;
  if (damping < 0.0f) damping = 0.0f;
  if (damping > 1.0f) damping = 1.0f;

  feedbackGain_ = feedbackGain;
  // Same "never let the one-pole coefficient reach exactly 0" reasoning as
  // FDNReverb::setParameters() - a literal 0 coefficient would freeze the
  // damping filter's state forever, crushing the whole tap, not just the
  // highs.
  dampingCoef_ = 1.0f - damping;
  if (dampingCoef_ < 0.01f) dampingCoef_ = 0.01f;
  pattern_ = pattern;
  patternSpeed_ = patternSpeed;

  int cap = static_cast<int>(buffer_.size());
  for (int i = 0; i < kNumTaps; i++) {
    float ratio = static_cast<float>(1 << i); // 1x/2x/4x/8x
    float seconds = baseRows * ratio * rowDurationSeconds;
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
