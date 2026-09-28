#include "PadSynthWavetable.h"

#include "SpectralBandProfile.h"
#include "../dsp/HashField.h"
#include "../dsp/RealFFT.h"

#include <algorithm>
#include <cmath>
#include <complex>

using namespace std;

namespace {

// Fixed compile-time salt, not per-instance - same reasoning as every
// other HashField call site in this codebase (see InstrumentVoice.h's own
// kNotePhaseSalt): the coordinate (bin_index, XORed with this table's own
// seed_ below) carries the actual per-bin variation, this salt just keeps
// PADsynth's own random-phase axis decorrelated from every other
// HashField-derived value anything else might draw.
constexpr uint64_t kPadSynthPhaseSalt = 0xD37C2A6F91B8E043ull;

// >= 2^18 samples at 48kHz (the task's own floor), scaled so a different
// output sample rate still gets an equivalently long/seamless table -
// rounded up to the next power of two for FFT efficiency (PocketFFT's own
// plan cache, dsp/RealFFT.h, handles any size, but a power of two avoids
// ever falling back to its slower non-power-of-two code path here).
size_t computeTableSize(int sample_rate) {
  constexpr size_t kBaseSize = size_t(1) << 18;
  constexpr int kBaseSampleRate = 48000;
  double scaled = static_cast<double>(kBaseSize) * static_cast<double>(sample_rate) / static_cast<double>(kBaseSampleRate);
  size_t size = kBaseSize;
  while (static_cast<double>(size) < scaled) size <<= 1;
  return size;
}

// Octave-region reference anchor - C0 (MIDI note 0, ~16.35Hz). Arbitrary
// but fixed: what matters is that every f0 maps to a unique, reproducible
// octave index and every octave's own table is generated at the exact
// same reference pitch every time, not the specific anchor chosen.
constexpr float kReferenceFrequency = 16.3516f;

} // namespace

PadSynthWavetable::PadSynthWavetable(int sample_rate, int partial_count, float bandwidth_cents,
                                      float bandwidth_scale_exponent, int edo_steps, int partial_limit,
                                      bool tuning_matched, uint64_t seed,
                                      float amplitude_rolloff_exponent,
                                      std::vector<PadSynthFormant> formants)
  : sample_rate_(sample_rate), table_size_(computeTableSize(sample_rate)),
    partial_count_(partial_count), bandwidth_cents_(bandwidth_cents),
    bandwidth_scale_exponent_(bandwidth_scale_exponent), edo_steps_(edo_steps),
    partial_limit_(partial_limit), tuning_matched_(tuning_matched), seed_(seed),
    amplitude_rolloff_exponent_(amplitude_rolloff_exponent), formants_(std::move(formants)) {
}

int
PadSynthWavetable::octaveFor(float f0) const {
  float safe_f0 = f0 > 0.0f ? f0 : kReferenceFrequency;
  return static_cast<int>(std::floor(std::log2(static_cast<double>(safe_f0) / static_cast<double>(kReferenceFrequency))));
}

float
PadSynthWavetable::referenceFrequencyFor(int octave) const {
  return kReferenceFrequency * std::exp2(static_cast<float>(octave));
}

PadSynthWavetable::CacheKey
PadSynthWavetable::keyFor(int octave) const {
  return CacheKey{ octave, partial_count_, bandwidth_cents_, bandwidth_scale_exponent_,
                    edo_steps_, partial_limit_, tuning_matched_, seed_ };
}

float
PadSynthWavetable::tableBaseFrequency(float f0) const {
  return referenceFrequencyFor(octaveFor(f0));
}

const std::vector<float> &
PadSynthWavetable::getTable(float f0) const {
  int octave = octaveFor(f0);
  auto key = keyFor(octave);
  auto it = tables_.find(key);
  if (it != tables_.end()) return it->second;
  auto [inserted, ok] = tables_.emplace(key, generateTable(octave));
  (void)ok;
  return inserted->second;
}

std::vector<float>
PadSynthWavetable::generateTable(int octave) const {
  size_t n = table_size_;
  float f0 = referenceFrequencyFor(octave);
  float bin_hz = static_cast<float>(sample_rate_) / static_cast<float>(n);
  float nyquist = static_cast<float>(sample_rate_) * 0.5f;

  RealFFT<float> fft(n);
  size_t bin_count = fft.binCount();
  std::vector<float> amplitude_spectrum(bin_count, 0.0f);

  // Standard cents-to-relative-ratio conversion (2^(cents/1200) - 1): the
  // fractional bandwidth a 1st-harmonic band would have at bandwidth_cents_
  // - see this class's own header comment for the full formula. Bandwidth
  // in Hz is anchored to f0 (the fundamental), not each harmonic's own
  // center frequency - the standard PADsynth formula is
  // bw_Hz(n) = f0 * base_ratio * n^bandwidth_scale_exponent, so the n^scale
  // growth is the *only* place harmonic number enters the bandwidth. Using
  // f_center(n) (~f0*n) here instead would silently multiply in an extra
  // factor of n, making relative bandwidth (sigma/f_center) blow up as
  // roughly n^(1+scale) instead of n^scale - at n=48 with scale=0.8 that's
  // the difference between a band ~1% of its own center frequency wide
  // (musical, recognizably harmonic) and one ~50% wide (heavily overlapping
  // neighboring harmonics into broadband noise) - confirmed by ear as the
  // actual bug behind an early "padsynth is mostly noise" report.
  //
  // The 1/(2*sqrt(2)) factor converts bw_Hz (as bw_Hz is defined above and
  // in every reference PADsynth implementation) into an actual Gaussian
  // standard deviation. Checked directly against Paul Nasca's own public-
  // domain reference implementation (PADsynth.cpp, per-gron/HSPad on
  // GitHub): its profile function is exp(-x^2) with x = distance/bwi and
  // bwi = bw_Hz/(2*samplerate) - i.e. its Gaussian's width parameter is
  // bw_Hz/2 in Hz, used in exp(-x^2) rather than the standard exp(-0.5*x^2)
  // form this class uses. Equating exp(-0.5*(d/sigma)^2) to
  // exp(-(d/(bw_Hz/2))^2) and solving for sigma gives
  // sigma = bw_Hz/(2*sqrt(2)) - using bw_Hz directly as sigma (this file's
  // very first version) made every band roughly 2.83x wider than the
  // reference implementation produces for the same bandwidth_cents value.
  constexpr float kBandwidthToSigma = 1.0f / (2.0f * 1.4142135623730951f);
  float base_bandwidth_hz = f0 * (std::exp2(bandwidth_cents_ / 1200.0f) - 1.0f) * kBandwidthToSigma;

  for (int n_harmonic = 1; n_harmonic <= partial_count_; n_harmonic++) {
    float ratio = tuningMatchedPartialRatio(n_harmonic, edo_steps_, partial_limit_, tuning_matched_);
    float f_center = f0 * ratio;
    if (f_center >= nyquist) break; // every higher harmonic is out of range too

    float sigma_hz = base_bandwidth_hz * std::pow(static_cast<float>(n_harmonic), bandwidth_scale_exponent_);
    // A harmonic whose bandwidth formula collapses to ~0 (e.g. bandwidth_cents_
    // == 0) would divide by zero below - floor it at one bin's own width so
    // it still lands as a single sharp spectral line instead of NaN.
    if (sigma_hz < bin_hz * 0.5f) sigma_hz = bin_hz * 0.5f;

    float harmonic_amplitude = 1.0f / std::pow(static_cast<float>(n_harmonic), amplitude_rolloff_exponent_);
    for (auto & formant : formants_) {
      float x = (f_center - formant.center_hz) / formant.bandwidth_hz;
      harmonic_amplitude *= 1.0f + (formant.gain - 1.0f) * std::exp(-0.5f * x * x);
    }

    // Normalized by the band's own width in bins (sigma_hz/bin_hz) so this
    // harmonic's TOTAL summed energy across its own band - not just its
    // peak bin - stays proportional to harmonic_amplitude alone, independent
    // of bandwidth. Without this, a peak-height Gaussian's own area under
    // the curve grows linearly with sigma (confirmed by direct summation:
    // sum_bins peak*exp(-0.5*x^2) ~= peak*sigma*sqrt(2*pi)/bin_hz) - so a
    // wider band (whether from a larger bandwidth_scale_exponent or a wider
    // bandwidth_cents) silently adds *loudness*, not just width, fighting
    // amplitude_rolloff_exponent's own intended falloff. In practice this
    // meant the *actual* per-harmonic energy falloff was always
    // (amplitude_rolloff_exponent - bandwidth_scale_exponent), far gentler
    // than the nominal exponent suggested (e.g. "warm"'s nominal 2.0 rolloff
    // behaved like 1.4) - confirmed by hand-summing the Gaussian - and is
    // the real reason repeated "steepen the rolloff" retuning kept
    // under-delivering on taming brightness/buzziness. This decouples
    // bandwidth (now purely a width/detuning-character knob) from loudness
    // (now purely amplitude_rolloff_exponent's own job), independent of
    // sigma's floor-clamp above.
    float peak_amplitude = harmonic_amplitude / (sigma_hz / bin_hz);

    // Only bins within a handful of standard deviations actually matter -
    // evaluating the Gaussian out to the whole spectrum for every harmonic
    // would be needlessly slow for a table this long.
    constexpr float kSigmaCutoff = 6.0f;
    float lo_hz = f_center - kSigmaCutoff * sigma_hz;
    float hi_hz = f_center + kSigmaCutoff * sigma_hz;
    size_t bin_lo = lo_hz > 0.0f ? static_cast<size_t>(lo_hz / bin_hz) : 0;
    size_t bin_hi = std::min(bin_count - 1, static_cast<size_t>(hi_hz / bin_hz) + 1);

    for (size_t bin = bin_lo; bin <= bin_hi; bin++) {
      float f_bin = static_cast<float>(bin) * bin_hz;
      float x = (f_bin - f_center) / sigma_hz;
      // Linear-amplitude Gaussian band, summed by plain addition across
      // harmonics (not RMS/power) - see this class's own header comment.
      amplitude_spectrum[bin] += peak_amplitude * std::exp(-0.5f * x * x);
    }
  }

  // One random phase per bin, drawn once the full spectrum (every
  // harmonic's band already summed in) is known - not one phase per band.
  // Coordinate is the bin index; the table's own seed_ (XORed into the
  // salt, following InstrumentVoice.h's own kNotePhaseSalt-combined-with-
  // note_coord convention) is what makes two differently-seeded
  // instruments draw independent phases for the same bin index.
  HashField phase_field(kPadSynthPhaseSalt ^ seed_);
  std::vector<std::complex<float>> spectrum(bin_count, std::complex<float>(0.0f, 0.0f));
  for (size_t bin = 0; bin < bin_count; bin++) {
    float amplitude = amplitude_spectrum[bin];
    if (amplitude <= 0.0f) continue;
    // DC and (for even n) Nyquist must stay purely real for a valid
    // real-signal spectrum - forcing phase to 0 there is exact, not an
    // approximation, and both bins carry negligible amplitude in practice
    // (harmonic 1's own band starts well above DC).
    float phase = (bin == 0 || bin == bin_count - 1) ? 0.0f
      : phase_field.range(static_cast<int64_t>(bin), paramId("padsynth_phase"), 0.0f, 2.0f * static_cast<float>(M_PI));
    spectrum[bin] = std::complex<float>(amplitude * std::cos(phase), amplitude * std::sin(phase));
  }

  // inverse() already normalizes by 1/size() - the resulting buffer is one
  // full period of the looping waveform, seamless by construction (any
  // inverse FFT of a discrete spectrum is exactly periodic in its own
  // length - see this class's own header comment and PadSynthVoice.h's
  // render() for how a voice reads it back at a different pitch). That
  // 1/size() scaling alone leaves the waveform's actual peak amplitude an
  // arbitrary function of table_size_/partial_count_/bandwidth (energy
  // spread across a quarter-million-plus samples, summed from many small
  // per-bin Gaussian contributions) - nowhere near Oscillator's own ±1
  // output - so it's rescaled here to a fixed target peak, the same
  // "level=1.0 means roughly full scale" contract every other Instrument
  // leaf's own `level` parameter already assumes.
  auto table = fft.inverse(spectrum);
  float peak = 0.0f;
  for (float sample : table) peak = std::max(peak, std::fabs(sample));
  if (peak > 0.0f) {
    constexpr float kTargetPeak = 0.9f;
    float scale = kTargetPeak / peak;
    for (float & sample : table) sample *= scale;
  }
  return table;
}
