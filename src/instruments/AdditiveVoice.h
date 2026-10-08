#ifndef _ADDITIVEVOICE_H_
#define _ADDITIVEVOICE_H_

#include "AdditiveModel.h"
#include "InstrumentVoice.h"
#include "SinusoidBank.h"
#include "../ambisonic/AmbisonicStackEncoder.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/NoteCoordinate.h"
#include "../dsp/HashField.h"
#include "../dsp/NoiseGenerator.h"
#include "../dsp/Vec8.h"

#include <algorithm>
#include <vector>
#include <cmath>
#include <memory>

namespace {
// Fixed compile-time seed for the attack noise burst's PRNG, one salt per
// feature as in InstrumentVoice.h's kNotePhaseSalt.
constexpr uint64_t kAdditiveAttackNoiseSalt = 0x2A6F91C4D57B8E30ull;

// The burst's decay time constant: -60 dB by this many seconds, so it is
// inaudible well before the bank's own audible life.
constexpr float kAttackNoiseDurationSec = 0.015f;
inline float attackNoiseAlpha() {
  static const float alpha = -logf(powf(10.0f, -60.0f / 20.0f)) / kAttackNoiseDurationSec;
  return alpha;
}
}

// The InstrumentVoice around a SinusoidBank plus a short attack noise burst.
// Each string is its own output row, placed at its own direction around the
// key's position (keyboard spread, string spread), and every row is encoded
// in one pass; the noise burst is one more row at the key's position. The
// floor reflection and the Aux sends run once on the summed dry signal.
class AdditiveVoice : public InstrumentVoice {
 public:
  AdditiveVoice(const ChannelConfiguration & config, const SphericalPosition & position, float detune, float level, float attack_noise_level,
                float keyboard_spread_deg, float string_spread_deg,
                const SendLevels & sends, const NoteCoordinate & note_coord)
      : InstrumentVoice(config, position, detune, sends, note_coord),
        level_(level),
        attack_noise_level_(attack_noise_level),
        keyboard_spread_deg_(keyboard_spread_deg),
        string_spread_deg_(string_spread_deg),
        noise_(seedFromCoord(note_coord)) {
  }

  // Builds the bank and arms the burst; called once, right after
  // playNote(), so getFrequency() and velocity_ already reflect this note-on.
  void trigger(const AdditiveModelParams & model, int edo_steps, const NoteCoordinate & note_coord) {
    float sample_rate = static_cast<float>(getChannelConfiguration().getAudioOutSampleRate());
    NoteContext note{getFrequency(), velocity_, sample_rate, edo_steps, note_coord};
    bank_ = std::make_unique<SinusoidBank>(buildPartialSpecs(model, note), sample_rate);

    const int strings = std::clamp(model.unison_voices, 1, 3);
    const float key_azimuth = keyboard_spread_deg_ * keyboardPosition(getFrequency());
    directions_.clear();
    for (int s = 0; s < strings; s++) {
      SphericalPosition direction = getPosition();
      direction.azimuth += key_azimuth + stringAzimuthOffsetDeg(s, strings, string_spread_deg_);
      directions_.push_back(direction);
    }

    if (attack_noise_level_ > 0.0f) {
      attack_noise_active_ = true;
      attack_noise_amp_ = 1.0f;
      attack_noise_decay_mult_ = expf(-attackNoiseAlpha() / sample_rate);
      SphericalPosition direction = getPosition();
      direction.azimuth += key_azimuth;
      directions_.push_back(direction);
    }

    gains_.resize(directions_.size());
    for (size_t r = 0; r < directions_.size(); r++) gains_[r] = computeAmbisonicGains(directions_[r]);
  }

  void adjustAzimuth(float delta) override {
    InstrumentVoice::adjustAzimuth(delta);
    for (size_t r = 0; r < directions_.size(); r++) {
      directions_[r].azimuth += delta;
      gains_[r] = computeAmbisonicGains(directions_[r]);
    }
  }

  AudioBuffer render(int frames) override {
    const float gain = decibelsToGain(getGainDB()) * level_;
    const size_t stride = (static_cast<size_t>(frames) + dsp::kLanes - 1) / dsp::kLanes * dsp::kLanes;
    const size_t rows = directions_.size();

    rows_.assign(rows * stride, 0.0f);
    if (bank_) bank_->render(rows_.data(), stride, frames);

    if (attack_noise_active_) {
      float * noise_row = rows_.data() + (rows - 1) * stride;
      for (int k = 0; k < frames; k++) {
        if (attack_noise_amp_ < 1e-4f) { attack_noise_active_ = false; break; }
        noise_row[k] += noise_.next() * attack_noise_amp_ * attack_noise_level_;
        attack_noise_amp_ *= attack_noise_decay_mult_;
      }
    }

    for (auto & s : rows_) s *= gain;

    stepForward(frames);

    const bool has_main = getSends().main > 0.0f;
    const float main_gain = getSends().main * getDistanceGain();
    AudioBuffer data = makeSendBuffer(frames);

    if (has_main && rows > 0) {
      targets_.resize(rows);
      for (size_t r = 0; r < rows; r++) {
        targets_[r] = gains_[r];
        for (auto & g : targets_[r]) g *= main_gain;
      }
      encoder_.encodeBlock(data, rows_.data(), stride, targets_, frames);
    }

    // The whole dry signal for the reflection and the sends, summed only
    // when something needs it.
    const float * dry = rows_.data();
    const bool reflect = has_main && floorReflectionActive();
    const bool sends = getSends().a > 0.0f || getSends().b > 0.0f;
    if (rows > 1 && (reflect || sends)) {
      sum_.assign(stride, 0.0f);
      for (size_t r = 0; r < rows; r++) {
        const float * row = rows_.data() + r * stride;
        for (size_t k = 0; k < stride; k++) sum_[k] += row[k];
      }
      dry = sum_.data();
    }
    if (rows > 0) {
      if (reflect) addFloorReflection(data, dry, frames, main_gain);
      addAuxSends(data, dry, frames);
    }
    return data;
  }

 private:
  static uint32_t seedFromCoord(const NoteCoordinate & note_coord) {
    return static_cast<uint32_t>(HashField(kAdditiveAttackNoiseSalt).unit(note_coord.toHashCoord(), paramId("additive_attack_noise_seed")) * 4294967295.0f);
  }

  float level_;
  float attack_noise_level_;
  float keyboard_spread_deg_, string_spread_deg_;
  std::unique_ptr<SinusoidBank> bank_;
  NoiseGenerator noise_;
  bool attack_noise_active_ = false;
  float attack_noise_amp_ = 0.0f;
  float attack_noise_decay_mult_ = 0.0f;
  std::vector<SphericalPosition> directions_; // one per string, then the noise burst
  std::vector<AmbisonicGains> gains_, targets_;
  AmbisonicStackEncoder encoder_;
  std::vector<float> rows_, sum_;
};

#endif
