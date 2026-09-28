#ifndef _PADSYNTHWAVETABLE_H_
#define _PADSYNTHWAVETABLE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

// One formant-style resonance boost applied on top of a harmonic's own
// base amplitude while a table is generated - PadSynthPresetParams'
// (PadSynthPresets.h) own extension for the "formant-vocal" preset, not
// part of the public PADsynth algorithm description itself. `gain` is a
// linear multiplier at the formant's own center_hz, tapering back to 1x
// (no effect) with a Gaussian falloff of standard deviation bandwidth_hz -
// several formants combine multiplicatively, one per vowel-like resonance.
struct PadSynthFormant {
  float center_hz, bandwidth_hz, gain;
};

// Paul Nasca's PADsynth algorithm, implemented from the public algorithm
// description only (Gaussian-shaped amplitude band per harmonic in
// frequency-bin space, one random phase per bin once every band's
// contribution to that bin is summed, single inverse FFT to resynthesize
// one full period of a seamlessly-looping wavetable).
//
// One instance serves a whole <padsynth> instrument node: every voice/note
// played through it shares this same table cache (see getTable()) - table
// generation is a real cost (one long inverse FFT per pitch region) that
// must happen at most once per region, not once per note.
//
// Formulas actually implemented (see PadSynthWavetable.cpp for where each
// applies):
//   - Per-harmonic bandwidth (standard deviation, in Hz) of the Gaussian
//     band centered at f_center = f0 * ratio(n):
//       sigma_hz = f0 * (2^(bandwidth_cents/1200) - 1) * n^bandwidth_scale_exponent
//     (2^(cents/1200) - 1 is the standard cents-to-relative-ratio
//     conversion - the fractional bandwidth a 1st-harmonic band would have
//     at `bandwidth_cents`; n^bandwidth_scale_exponent is what makes
//     higher harmonics progressively wider, controlled by the caller.
//     Anchored to f0, not f_center(n) - see PadSynthWavetable.cpp's own
//     comment on why using f_center here would silently blow up relative
//     bandwidth by an extra factor of n.)
//   - Per-bin Gaussian amplitude profile (linear amplitude, not power),
//     peak-normalized by the band's own width in bins (sigma_hz/bin_hz) so
//     a harmonic's TOTAL summed energy across its band stays proportional
//     to harmonic_amplitude(n) alone, independent of bandwidth - a
//     peak-height Gaussian's own area otherwise grows linearly with sigma,
//     which would silently add loudness as bandwidth widens and decouple
//     amplitude_rolloff_exponent from what a preset's partials actually
//     sound like:
//       peak_amplitude(n) = harmonic_amplitude(n) / (sigma_hz(n) / bin_hz)
//       amplitude(f_bin) = peak_amplitude(n) * exp(-0.5 * ((f_bin - f_center) / sigma_hz)^2)
//   - Every harmonic's band is summed into the full spectrum by plain
//     linear-amplitude addition (matching PADsynth's own public
//     description - not a power/RMS sum): amplitude_spectrum[bin] +=
//     amplitude(f_bin) for every harmonic whose band reaches that bin.
//   - Exactly one random phase per bin is drawn (via HashField, see
//     PadSynthWavetable.cpp) from the *final* summed amplitude spectrum,
//     not per-band - so two harmonics whose bands overlap the same bin
//     still get one shared, coherent phase there, not two independently
//     rotated contributions that would otherwise partially cancel.
class PadSynthWavetable {
 public:
  // sample_rate/partial_count/bandwidth_cents/bandwidth_scale_exponent/
  // edo_steps/partial_limit/tuning_matched/seed are exactly the PADsynth
  // parameters described in the task; amplitude_rolloff_exponent and
  // formants are this class's own extension (see PadSynthFormant above)
  // needed to give presets like "glass" vs. "bowed-ensemble" a genuinely
  // different harmonic-amplitude character, not just a different
  // bandwidth - harmonic n's own base amplitude (before the Gaussian band
  // and any formant boost) is 1/n^amplitude_rolloff_exponent, the
  // standard sawtooth-like 1/n falloff at the default exponent of 1.0.
  PadSynthWavetable(int sample_rate, int partial_count, float bandwidth_cents,
                     float bandwidth_scale_exponent, int edo_steps, int partial_limit,
                     bool tuning_matched, uint64_t seed,
                     float amplitude_rolloff_exponent = 1.0f,
                     std::vector<PadSynthFormant> formants = {});

  // Returns the wavetable covering f0's own pitch region, building and
  // caching it on first use (one table per octave region - see .cpp).
  const std::vector<float> & getTable(float f0) const;

  // The pitch region's own reference fundamental (Hz) that getTable(f0)'s
  // table was actually generated at - see PadSynthVoice.h's own render()
  // for how a voice resamples from this to its note's real pitch.
  float tableBaseFrequency(float f0) const;

  int getSampleRate() const { return sample_rate_; }
  size_t getTableSize() const { return table_size_; }

 private:
  // Every parameter that shapes a table's content, plus the octave region
  // it's for - a plain float-only cache key (e.g. just f0) could silently
  // collide two different preset configurations that happen to ask for
  // the same octave, so every generation input is part of the key even
  // though, today, only octave ever actually varies across calls on the
  // same instance (the rest are fixed at construction) - this stays
  // correct if that ever stops being true.
  struct CacheKey {
    int octave;
    int partial_count;
    float bandwidth_cents;
    float bandwidth_scale_exponent;
    int edo_steps;
    int partial_limit;
    bool tuning_matched;
    uint64_t seed;

    bool operator==(const CacheKey & other) const noexcept {
      return octave == other.octave && partial_count == other.partial_count
        && bandwidth_cents == other.bandwidth_cents && bandwidth_scale_exponent == other.bandwidth_scale_exponent
        && edo_steps == other.edo_steps && partial_limit == other.partial_limit
        && tuning_matched == other.tuning_matched && seed == other.seed;
    }
  };
  struct CacheKeyHash {
    size_t operator()(const CacheKey & key) const noexcept {
      // Plain FNV-1a-style mixing over each field's own bit pattern -
      // collision-resistance doesn't need to be cryptographic here, just
      // good enough to avoid pathological unordered_map bucket pileup.
      uint64_t h = 1469598103934665603ull;
      auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
      auto floatBits = [](float f) { uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); return bits; };
      mix(static_cast<uint64_t>(key.octave));
      mix(static_cast<uint64_t>(key.partial_count));
      mix(static_cast<uint64_t>(floatBits(key.bandwidth_cents)));
      mix(static_cast<uint64_t>(floatBits(key.bandwidth_scale_exponent)));
      mix(static_cast<uint64_t>(key.edo_steps));
      mix(static_cast<uint64_t>(key.partial_limit));
      mix(key.tuning_matched ? 1ull : 0ull);
      mix(key.seed);
      return static_cast<size_t>(h);
    }
  };

  int octaveFor(float f0) const;
  float referenceFrequencyFor(int octave) const;
  std::vector<float> generateTable(int octave) const;
  CacheKey keyFor(int octave) const;

  int sample_rate_;
  size_t table_size_;
  int partial_count_;
  float bandwidth_cents_;
  float bandwidth_scale_exponent_;
  int edo_steps_;
  int partial_limit_;
  bool tuning_matched_;
  uint64_t seed_;
  float amplitude_rolloff_exponent_;
  std::vector<PadSynthFormant> formants_;

  // Lazily built/cached per octave region - mutable so getTable() (the
  // read-only public API every voice calls from render(), a const method)
  // can fill it on first use, matching SampleContent.h's own
  // waveform_peaks_/stretched_buffer_ lazy-cache precedent.
  mutable std::unordered_map<CacheKey, std::vector<float>, CacheKeyHash> tables_;
};

#endif
