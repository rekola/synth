#include "TapeTransport.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

// Self-contained (not TreeNode::decibelsToGain()/gainToDecibels(), which
// only free-standing TreeNode<Derived> subclasses - VoiceState/TrackState
// - can reach) - the same "each DSP file keeps its own small dB helper"
// convention effects/Compressor.cpp's db2lin()/lin2db() already uses.
inline float dbToLinear(float db) { return powf(10.0f, db * 0.05f); }

inline float uniform01(NoiseGenerator & rng) { return 0.5f * (rng.next() + 1.0f); }

}

float
TapeTransportDsp::drawExponentialSeconds(NoiseGenerator & rng, float rateHz) {
  if (rateHz <= 0.0f) return 1.0e9f; // effectively "never" - Poisson process disabled
  float u = std::max(uniform01(rng), 1.0e-6f); // avoid log(0)
  return -logf(1.0f - u) / rateHz;
}

TapeTransportSample
TapeTransportDsp::nextSample(const TapeTransportParams & params, float sampleRate) {
  float dt = 1.0f / sampleRate;
  elapsed_seconds_ += dt;

  // Health: white noise low-passed at healthRateHz, ceilinged at 1.0 and
  // dipping toward 1 - healthSensitivity as it wanders - see TapeTransport.h's
  // own doc comment on why a ceiling (rather than reverting toward a
  // center) is enough to read as "occasional dip, recovers" on its own.
  // decayMode (Disintegration) layers a monotonically-growing offset
  // underneath that ceiling, driven off elapsed_seconds_ rather than a
  // separate accumulator - it can only grow (elapsed_seconds_ never goes
  // backward), so material erodes but never spontaneously recovers.
  float alpha = std::min(1.0f, params.healthRateHz * dt * kTwoPi);
  raw_health_ += (white_.next() - raw_health_) * alpha;
  raw_health_ = std::max(-1.0f, std::min(1.0f, raw_health_));
  float decay_offset = params.decayMode ? std::min(1.0f, params.decayRatePerMinute * (elapsed_seconds_ / 60.0f)) : 0.0f;
  float health = std::max(0.0f, 1.0f - params.healthSensitivity * 0.5f * (1.0f - raw_health_) - decay_offset);
  float trouble = 1.0f - health; // 0 = nominal, up to 1 at its worst (healthSensitivity normally, more under decayMode)

  // Wow + flutter, health-scaled depth. Locked wow (Vinyl) skips the
  // health scaling entirely - a turntable's rotational wow doesn't get
  // worse because the tape transport is having a bad moment, it's a
  // fixed mechanical rate.
  float depthScale = 1.0f + trouble;
  wow_phase_ += params.wowRateHz * dt;
  wow_phase_ -= floorf(wow_phase_);
  locked_wow_phase_ += params.wowLockedRateHz * dt;
  locked_wow_phase_ -= floorf(locked_wow_phase_);
  flutter_phase_ += params.flutterRateHz * dt;
  flutter_phase_ -= floorf(flutter_phase_);

  float wow_phase_used = params.wowLocked ? locked_wow_phase_ : wow_phase_;
  float wow_depth_scale = params.wowLocked ? 1.0f : depthScale;
  float pitch_cents = params.wowDepthCents * sinf(kTwoPi * wow_phase_used) * wow_depth_scale
                     + params.flutterDepthCents * sinf(kTwoPi * flutter_phase_) * depthScale;

  // Pressure-pad amplitude flutter (Mellotron) - rides the same flutter
  // phase as the pitch flutter above (one mechanical wobble, two audible
  // symptoms - see TapeTransport.h), health-scaled the same way.
  float amp_flutter = 1.0f + params.ampFlutterDepth * sinf(kTwoPi * flutter_phase_) * depthScale;

  // Dropouts (Poisson-timed gain dips) - effective rate scales with
  // trouble, same as everything else, so a "bad moment" for the
  // transport also means dropouts get more likely right then, not on an
  // independent clock.
  if (!dropout_timer_seeded_) {
    dropout_timer_ = drawExponentialSeconds(white_, params.dropoutRateHz);
    dropout_timer_seeded_ = true;
  }
  if (dropout_remaining_ > 0.0f) {
    dropout_remaining_ -= dt;
  } else {
    dropout_timer_ -= dt;
    if (dropout_timer_ <= 0.0f) {
      dropout_remaining_ = params.dropoutDurationMs * 0.001f;
      dropout_timer_ = drawExponentialSeconds(white_, params.dropoutRateHz * (1.0f + trouble));
    }
  }
  float dropout_gain = dropout_remaining_ > 0.0f ? dbToLinear(params.dropoutDepthDB) : 1.0f;

  // Clicks (Poisson-timed impulses - "filtered" comes from whatever
  // biquad stage sees this impulse downstream in TapeDegradationDsp, not
  // from any shaping here).
  if (!click_timer_seeded_) {
    click_timer_ = drawExponentialSeconds(white_, params.clickRateHz);
    click_timer_seeded_ = true;
  }
  click_timer_ -= dt;
  float click_impulse = 0.0f;
  if (click_timer_ <= 0.0f) {
    click_impulse = dbToLinear(params.clickGainDB) * (white_.next() >= 0.0f ? 1.0f : -1.0f);
    click_timer_ = drawExponentialSeconds(white_, params.clickRateHz * (1.0f + trouble));
  }

  // Hiss: pink-filtered white noise, level- and health-scaled.
  float hiss = pink_.process(white_.next()) * dbToLinear(params.hissLevelDB) * (1.0f + trouble);

  // Rumble (Vinyl) - a second, independent noise source, one-pole
  // low-passed rather than pink-filtered (see TapeTransport.h's own doc
  // comment on why) - not health-scaled, since motor/bearing rumble
  // doesn't correlate with the tape-transport-style faults every other
  // component here models.
  float rumble_alpha = std::min(1.0f, params.rumbleHz * dt * kTwoPi);
  rumble_state_ += (white_.next() - rumble_state_) * rumble_alpha;
  float rumble = rumble_state_ * dbToLinear(params.rumbleLevelDB);

  return TapeTransportSample{ pitch_cents, dropout_gain, click_impulse, hiss, health, amp_flutter, rumble };
}
