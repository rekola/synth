#ifndef _ADDITIVEVOICE_H_
#define _ADDITIVEVOICE_H_

#include "InstrumentVoice.h"
#include "SinusoidBank.h"
#include "../dsp/SpectralEnvelopeRemap.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/NoteCoordinate.h"
#include "../dsp/HashField.h"
#include "../dsp/NoiseGenerator.h"

#include <vector>
#include <cmath>
#include <memory>

namespace {
// Fixed compile-time seed for the attack-transient noise burst's own PRNG
// seed - see InstrumentVoice.h's kNotePhaseSalt/Noise.cpp's kNoiseSeedSalt
// for the identical one-salt-per-feature reasoning.
constexpr uint64_t kAdditiveAttackNoiseSalt = 0x2A6F91C4D57B8E30ull;

// The attack burst's own fixed decay time constant - short enough that
// it's fully inaudible well before the sinusoid bank's own audible life (a
// decayed-below-noise-floor burst by ~15ms), independent of the bank's own
// per-partial decay rates. -60dB by kAttackNoiseDurationSec:
// alpha = -ln(10^(-60/20)) / duration.
constexpr float kAttackNoiseDurationSec = 0.015f;
inline float attackNoiseAlpha() {
  static const float alpha = -logf(powf(10.0f, -60.0f / 20.0f)) / kAttackNoiseDurationSec;
  return alpha;
}

// Reference velocity ([0,1], see Note::getVelocityAsFloat()) that
// tilt/velocityTilt is measured around - a mid-velocity hit (0.5) neither
// brightens nor dulls the preset's own base tilt; a harder hit (velocity
// approaches 1.0) adds up to +velocityTilt dB/octave (less negative tilt,
// brighter), a softer one subtracts from it (darker) - see Additive.h's
// own doc comment for the exact formula.
constexpr float kReferenceVelocity = 0.5f;
}

// The InstrumentVoice-integrated wrapper around a SinusoidBank (the
// technical DSP building block, SinusoidBank.h) plus a short attack-noise
// burst standing in for a hammer/pluck transient - see Additive.h's own
// doc comment for the split between the two. Owns no timbral computation
// of its own beyond resolving velocity->tilt and reading its own
// (already-detuned) frequency - everything else is straight construction
// parameters handed to SinusoidBank.
class AdditiveVoice : public InstrumentVoice {
 public:
  AdditiveVoice(const ChannelConfiguration & config, const SphericalPosition & position, float detune, float level, float attack_noise_level,
                const SendLevels & sends, const NoteCoordinate & note_coord)
    : InstrumentVoice(config, position, detune, sends, note_coord),
      level_(level), attack_noise_level_(attack_noise_level),
      noise_(seedFromCoord(note_coord)) {
  }

  // Builds the SinusoidBank and arms the attack-noise burst - called once,
  // right after playNote() (so getFrequency()/velocity_ already reflect
  // this note-on), from Additive::playNote(). tilt_db/velocity_tilt_db are
  // the *base* (unresolved) values from the XML/preset; the actual tilt
  // used is resolved here from this voice's own velocity_.
  void trigger(int partial_count, float tilt_db, float velocity_tilt_db, float inharmonicity_b,
               int edo_steps, bool tuning_matched, int partial_limit,
               float decay_a, float decay_b, float decay_p,
               int unison_voices, float unison_detune_cents,
               float envelope_anchor_hz, float envelope_tracking,
               SpectralPostprocessKind postprocess_kind, int postprocess_n, int postprocess_r, float postprocess_amount,
               const NoteCoordinate & note_coord) {
    float sample_rate = static_cast<float>(getChannelConfiguration().getAudioOutSampleRate());
    float effective_tilt = tilt_db + velocity_tilt_db * (velocity_ - kReferenceVelocity);

    SinusoidBank::Params params{
      getFrequency(), partial_count, effective_tilt, inharmonicity_b,
      edo_steps, tuning_matched, partial_limit,
      decay_a, decay_b, decay_p,
      unison_voices, unison_detune_cents,
      sample_rate,
      envelope_anchor_hz, envelope_tracking,
      postprocess_kind, postprocess_n, postprocess_r, postprocess_amount
    };
    bank_ = std::make_unique<SinusoidBank>(params, note_coord);

    if (attack_noise_level_ > 0.0f) {
      attack_noise_active_ = true;
      attack_noise_amp_ = 1.0f;
      attack_noise_decay_mult_ = expf(-attackNoiseAlpha() / sample_rate);
    }
  }

  AudioBuffer render(int frames) override {
    float gain = decibelsToGain(getGainDB()) * level_;

    if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));
    for (auto & s : dry_) s = 0.0f;

    if (bank_) bank_->render(dry_.data(), frames);

    if (attack_noise_active_) {
      for (int k = 0; k < frames; k++) {
        if (attack_noise_amp_ < 1e-4f) { attack_noise_active_ = false; break; }
        dry_[static_cast<size_t>(k)] += noise_.next() * attack_noise_amp_ * attack_noise_level_;
        attack_noise_amp_ *= attack_noise_decay_mult_;
      }
    }

    for (auto & s : dry_) s *= gain;

    stepForward(frames);

    return encodePosition(dry_.data(), frames);
  }

 private:
  static uint32_t seedFromCoord(const NoteCoordinate & note_coord) {
    return static_cast<uint32_t>(HashField(kAdditiveAttackNoiseSalt).unit(note_coord.toHashCoord(), paramId("additive_attack_noise_seed")) * 4294967295.0f);
  }

  float level_;
  float attack_noise_level_;
  std::unique_ptr<SinusoidBank> bank_;
  NoiseGenerator noise_;
  bool attack_noise_active_ = false;
  float attack_noise_amp_ = 0.0f;
  float attack_noise_decay_mult_ = 0.0f;
  std::vector<float> dry_;
};

#endif
