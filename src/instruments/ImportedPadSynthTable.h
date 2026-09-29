#ifndef _IMPORTEDPADSYNTHTABLE_H_
#define _IMPORTEDPADSYNTHTABLE_H_

#include "PadSynthTableSource.h"
#include "ImportedOscillatorChain.h"
#include "ImportedPadSynthProfile.h"
#include "../dsp/SpectralEnvelopeRemap.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

// PADsynth rendering (docs/padsynth.md) for an imported (ZynAddSubFX-
// sourced) preset - the clean-room-implemented, profile-placement
// counterpart to PadSynthWavetable's own from-scratch Gaussian-band
// renderer. Both implement PadSynthTableSource, so PadSynthVoice reads
// through either without caring which; PadSynth::ensureWavetable() is
// what picks between them (PadSynthPresetParams::imported).
//
// Unlike PadSynthWavetable (one table per octave region, resampled from a
// single fixed C0-anchored reference pitch), this class's own sample set
// is the preset's own real ZynAddSubFX sample layout: f_b/octaves/
// samples-per-octave/L, all real ported data (docs/padsynth.md's "Sample
// set"). getTable(f0) picks the nearest of the preset's own M sample
// points by log-frequency distance, matching how ZynAddSubFX itself
// selects among a PADsynth instrument's own precomputed samples.
struct ImportedPadSynthParams {
  ImportedOscillator::OscillatorChainParams oscillator;
  ImportedPadSynth::ProfileParams profile;
  ImportedPadSynth::PositionParams position;

  float bandwidth_cents = 10.0f;

  // Sample set (docs/padsynth.md).
  float base_frequency_hz = 261.6f; // f_b
  int octaves = 6;                  // O
  int samples_per_octave = 2;       // q
  int table_length = 1 << 17;       // L

  int edo_steps = 31;
  bool tuning_matched = true;

  // Anchored spectral-envelope remap + postprocess (dsp/
  // SpectralEnvelopeRemap.h) - step 9 of the chain, shared with
  // PadSynthWavetable/SinusoidBank.
  float envelope_anchor_hz = 0.0f;
  float envelope_tracking = 0.0f;
  SpectralPostprocessKind postprocess_kind = SpectralPostprocessKind::None;
  int postprocess_n = 0;
  int postprocess_r = 0;
  float postprocess_amount = 0.0f;

  uint64_t seed = 1;
};

class ImportedPadSynthTable : public PadSynthTableSource {
 public:
  ImportedPadSynthTable(int sample_rate, ImportedPadSynthParams params);

  const std::vector<float> & getTable(float f0) const override;
  float tableBaseFrequency(float f0) const override;
  int getSampleRate() const override { return sample_rate_; }

 private:
  int sampleCount() const { return params_.octaves * params_.samples_per_octave; }
  float sampleFrequency(int j) const;
  int nearestSampleIndex(float f0) const;
  std::vector<float> renderSample(int j) const;

  int sample_rate_;
  ImportedPadSynthParams params_;

  // The oscillator chain's own A[h] (steps 1-8) is frequency-independent -
  // computed once, lazily, and shared by every sample's own step 9/
  // PADsynth placement (which alone vary per sample's own base frequency).
  mutable std::vector<float> oscillator_magnitudes_;
  mutable bool oscillator_magnitudes_built_ = false;
  const std::vector<float> & oscillatorMagnitudes() const;

  mutable std::unordered_map<int, std::vector<float>> tables_;
};

#endif
