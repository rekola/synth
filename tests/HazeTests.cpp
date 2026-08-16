#include "TestFramework.h"

#include "../src/bus/Haze.h"
#include "../src/dsp/RealFFT.h"
#include "../src/dsp/NoiseGenerator.h"
#include "../src/dsp/PinkNoiseFilter.h"
#include "../src/state/MemoryParameterSource.h"

#include <cmath>
#include <complex>
#include <vector>

using namespace std;

namespace {

constexpr int kSampleRate = 44100;
constexpr int kWindow = 4096;
constexpr int kFundamentalBin = 37; // ~398Hz - comfortably inside every band used below
constexpr float kCyclesPerSample = static_cast<float>(kFundamentalBin) / static_cast<float>(kWindow);

vector<float> makeSine(int n, float cyclesPerSample, float amplitude = 0.5f) {
  vector<float> out(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) {
    out[static_cast<size_t>(i)] = amplitude * sinf(2.0f * static_cast<float>(M_PI) * cyclesPerSample * static_cast<float>(i));
  }
  return out;
}

vector<float> makePinkNoise(int n, uint32_t seed, float amplitude = 0.3f) {
  NoiseGenerator gen(seed);
  PinkNoiseFilter pink;
  vector<float> out(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) out[static_cast<size_t>(i)] = amplitude * pink.process(gen.next());
  return out;
}

// Runs `input` through `sat`, returning its full getChainSendSum() output.
vector<float> run(Haze & sat, const vector<float> & input) {
  int frames = static_cast<int>(input.size());
  sat.process(input.data(), frames);
  vector<float> out(static_cast<size_t>(frames));
  sat.getChainSendSum(out.data(), frames);
  return out;
}

float magnitudeAtBin(const vector<float> & signal, size_t windowSize, size_t bin) {
  vector<float> tail(signal.end() - static_cast<long>(windowSize), signal.end());
  RealFFT<float> fft(windowSize);
  auto & spectrum = fft.forward(tail);
  return abs(spectrum[bin]);
}

double rms(const vector<float> & signal, size_t skip) {
  double energy = 0.0;
  size_t n = 0;
  for (size_t i = skip; i < signal.size(); i++) {
    energy += static_cast<double>(signal[i]) * signal[i];
    n++;
  }
  return sqrt(energy / static_cast<double>(n));
}

}

TEST(haze_bias_increases_even_harmonic_content) {
  auto input = makeSine(kWindow * 2, kCyclesPerSample);

  Haze low(kSampleRate);
  low.setParameters(9.0f, SaturatorShape::Tanh, 0.0f, 20.0f, 16000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);
  auto outLow = run(low, input);

  Haze high(kSampleRate);
  high.setParameters(9.0f, SaturatorShape::Tanh, 0.6f, 20.0f, 16000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);
  auto outHigh = run(high, input);

  float secondHarmonicLow = magnitudeAtBin(outLow, kWindow, kFundamentalBin * 2);
  float secondHarmonicHigh = magnitudeAtBin(outHigh, kWindow, kFundamentalBin * 2);

  CHECK(secondHarmonicHigh > secondHarmonicLow * 2.0f);
}

TEST(haze_drive_increases_harmonic_content) {
  auto input = makeSine(kWindow * 2, kCyclesPerSample);

  Haze low(kSampleRate);
  low.setParameters(3.0f, SaturatorShape::SoftClip, 0.0f, 20.0f, 16000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);
  auto outLow = run(low, input);

  Haze high(kSampleRate);
  high.setParameters(30.0f, SaturatorShape::SoftClip, 0.0f, 20.0f, 16000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);
  auto outHigh = run(high, input);

  float thirdHarmonicLow = magnitudeAtBin(outLow, kWindow, kFundamentalBin * 3);
  float thirdHarmonicHigh = magnitudeAtBin(outHigh, kWindow, kFundamentalBin * 3);

  CHECK(thirdHarmonicHigh > thirdHarmonicLow * 2.0f);
}

TEST(haze_auto_gain_keeps_level_roughly_flat_across_drive) {
  // Amplitude 1.0 - matches the auto-gain LUT's own calibration reference
  // (Haze.cpp's compensationTable(), which scales its reference signal by
  // 1.0 too) - a static per-shape/drive/bias LUT is only ever calibrated
  // against one assumed input level (see plans/drum-bus-saturator.md's
  // "static curve integral, not a running RMS" decision), so testing
  // flatness at a level other than the calibration reference isn't a fair
  // test of the mechanism itself.
  auto input = makePinkNoise(kWindow * 3, 0xBEEF, 1.0f);

  vector<double> levels;
  for (float driveDb : { 0.0f, 9.0f, 18.0f, 27.0f, 36.0f }) {
    Haze sat(kSampleRate);
    sat.setParameters(driveDb, SaturatorShape::Tanh, 0.3f, 20.0f, 16000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);
    auto out = run(sat, input);
    levels.push_back(rms(out, static_cast<size_t>(kWindow))); // skip settle
  }

  double minLevel = levels[0], maxLevel = levels[0];
  for (double l : levels) {
    minLevel = min(minLevel, l);
    maxLevel = max(maxLevel, l);
  }
  CHECK(minLevel > 0.0);
  // Generous but meaningful: without compensation this ratio would be
  // many multiples (driveLinear_ alone spans 0-36dB, a 63x range) - the
  // compensation should collapse that down to a modest spread.
  CHECK(maxLevel / minLevel < 2.0);
}

TEST(haze_bandpass_attenuates_out_of_band_content) {
  Haze sat(kSampleRate);
  sat.setParameters(0.0f, SaturatorShape::Tanh, 0.0f, 200.0f, 1000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);

  int inBandBin = kFundamentalBin; // ~398Hz, inside 200-1000Hz
  // ~8011Hz - 3 octaves above the 1000Hz lpf, so a single 2nd-order
  // (12dB/octave) stage gives a clean ~36dB/63x margin; a bin only an
  // octave or so above the corner wouldn't leave enough headroom above
  // this test's own threshold to be a meaningful check.
  int outOfBandBin = 744;
  auto inBand = makeSine(kWindow * 2, static_cast<float>(inBandBin) / kWindow, 0.3f);
  auto outOfBandInput = makeSine(kWindow * 2, static_cast<float>(outOfBandBin) / kWindow, 0.3f);

  Haze sat2(kSampleRate);
  sat2.setParameters(0.0f, SaturatorShape::Tanh, 0.0f, 200.0f, 1000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);

  auto outInBand = run(sat, inBand);
  auto outOutOfBand = run(sat2, outOfBandInput);

  float magInBand = magnitudeAtBin(outInBand, kWindow, static_cast<size_t>(inBandBin));
  float magOutOfBand = magnitudeAtBin(outOutOfBand, kWindow, static_cast<size_t>(outOfBandBin));

  CHECK(magInBand > magOutOfBand * 10.0f);
}

TEST(haze_all_shapes_produce_finite_bounded_output) {
  auto input = makePinkNoise(4096, 0xABCD, 0.8f);

  for (auto shape : { SaturatorShape::Tanh, SaturatorShape::Asym, SaturatorShape::SoftClip, SaturatorShape::Fold }) {
    Haze sat(kSampleRate);
    sat.setParameters(36.0f, shape, 1.0f, 20.0f, 16000.0f, 12.0f, 12.0f, HazePreDelayDivision::OneOver128, 1.0f);
    auto out = run(sat, input);
    for (float s : out) {
      CHECK(isfinite(s));
      CHECK(fabs(s) < 100.0f);
    }
  }
}

TEST(haze_silence_in_is_silence_out) {
  // bias=0 specifically: with no DC injected into the shaper at all,
  // every stage (biquads, oversampler, DC blocker) starts and stays at
  // exactly zero for exactly-zero input, so this is the one case that
  // can assert an exact (not just small) zero.
  Haze sat(kSampleRate);
  sat.setParameters(9.0f, SaturatorShape::Tanh, 0.0f, 200.0f, 5000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);
  vector<float> silence(4096, 0.0f);
  auto out = run(sat, silence);
  for (float s : out) {
    CHECK_NEAR(s, 0.0f, 1e-6f);
  }
}

TEST(haze_no_startup_click_with_nonzero_bias) {
  // A nonzero bias deliberately injects DC into the shaper (that's how it
  // raises even-order content - see applyShape()'s own comment), so
  // literal silence still resolves to a real, nonzero steady-state DC
  // term inside the shaper. Regression test for a real bug: setParameters()
  // used to leave every downstream stage (the oversampler/decimator's own
  // FIR history, the DC blocker) at cold zero state, so the first block
  // of real (even silent) audio after construction/a preset change forced
  // all of that state to settle from scratch - audible as a sharp,
  // unprompted click (measured: silence in, ~0.2 amplitude within two
  // samples, with the default "glue" preset) rather than the small,
  // gradual DC-blocker residual settling itself would produce on its own.
  // setParameters() now warms the whole chain up against silence before
  // any real audio reaches it, so this must stay small and bounded from
  // sample 0 - not just eventually converge.
  Haze sat(kSampleRate); // default "glue" preset - bias=0.3
  vector<float> silence(64, 0.0f);
  auto out = run(sat, silence);
  for (float s : out) {
    CHECK(fabs(s) < 0.01f);
  }
}

TEST(haze_dc_blocker_does_not_get_stuck_above_true_zero) {
  // Regression test for a real bug: a one-pole decay (the DC blocker)
  // only asymptotes toward 0 in exact arithmetic, and in float32 can get
  // permanently pinned at a small but nonzero value instead - confirmed
  // by direct measurement: with the default "glue" preset (bias=0.3),
  // continuous silent input settled at a stable, non-decaying ~1.4e-6
  // plateau (nowhere near true zero, and squarely large enough to read
  // as real signal to an energy-based analyzer like DiracAnalyzer)
  // rather than continuing to decay, persisting for as long as the
  // effect kept running. process() now snaps the DC blocker's state
  // once it decays below a threshold confirmed (by repeated direct
  // measurement against this exact chain) to reliably clear that stuck
  // point - continuous silence must settle far below it, not merely
  // "smaller than before." The bound checked here (1e-8) sits two full
  // orders of magnitude under the snap threshold itself and ~100,000x
  // under the original bug's measured plateau, while still tolerating
  // the float32 noise floor a real audio chain settles at rather than
  // demanding bit-exact 0 (see AmbisonicDiffuseEncoderTests.cpp and
  // similar for why exact-0 assertions are reserved for cases with no
  // arithmetic between the zero source and the check).
  Haze sat(kSampleRate); // default "glue" preset - bias=0.3
  int frames = 4096;
  vector<float> silence(static_cast<size_t>(frames), 0.0f);
  // 200 blocks * 4096 = 819,200 samples - comfortably past (~3x) the
  // point where full convergence was directly measured (well under
  // 300,000 samples), and still cheap (plain float ops, no I/O).
  vector<float> out(static_cast<size_t>(frames));
  for (int block = 0; block < 200; block++) {
    sat.process(silence.data(), frames);
  }
  sat.getChainSendSum(out.data(), frames);
  for (float s : out) {
    CHECK(fabs(s) < 1e-8f);
  }
}

TEST(haze_predelay_resolves_as_a_fraction_of_a_whole_note) {
  // Default row duration (90bpm, matching Song's own default - no
  // setRowDuration() call) - a whole note is 16 rows, so e.g. 1/128
  // resolves to (1/128) * 16 * rowDuration, not (1/128) * rowDuration (a
  // fraction of a single short row would be under 2ms at any working
  // tempo - see recomputePredelaySamples()'s own comment).
  float rowDuration = 60.0f / 4.0f / 90.0f;
  float wholeNote = 16.0f * rowDuration;

  Haze sat128(kSampleRate);
  sat128.setParameters(9.0f, SaturatorShape::Tanh, 0.3f, 200.0f, 5000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);
  int expected128 = static_cast<int>((wholeNote / 128.0f) * kSampleRate + 0.5f);
  CHECK(sat128.getPredelaySamples() == expected128);

  Haze sat256(kSampleRate);
  sat256.setParameters(9.0f, SaturatorShape::Tanh, 0.3f, 200.0f, 5000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver256, 1.0f);
  int expected256 = static_cast<int>((wholeNote / 256.0f) * kSampleRate + 0.5f);
  CHECK(sat256.getPredelaySamples() == expected256);

  // 1/128 (a longer division) should resolve to roughly twice 1/256's
  // sample count, since both are fractions of the same whole note.
  CHECK(sat128.getPredelaySamples() > sat256.getPredelaySamples() * 1.8);
  CHECK(sat128.getPredelaySamples() < sat256.getPredelaySamples() * 2.2);
}

TEST(haze_predelay_clamps_at_extreme_slow_tempo) {
  // A very slow tempo pushes even the shortest division (1/256) past the
  // 40ms ceiling - must clamp there rather than let the delay grow
  // unbounded (comb filtering / an audible slapback at extreme tempos).
  Haze sat(kSampleRate);
  sat.setParameters(9.0f, SaturatorShape::Tanh, 0.3f, 200.0f, 5000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver256, 1.0f);
  sat.setRowDuration(2.0f); // an absurdly slow tempo (~7.5bpm)
  int expectedClamped = static_cast<int>(0.040f * kSampleRate + 0.5f);
  CHECK(sat.getPredelaySamples() == expectedClamped);
}

TEST(haze_predelay_clamps_at_extreme_fast_tempo) {
  // A very fast tempo pushes even the longest division (1/64) under the
  // 4ms floor - must clamp there rather than shrink toward zero.
  Haze sat(kSampleRate);
  sat.setParameters(9.0f, SaturatorShape::Tanh, 0.3f, 200.0f, 5000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver64, 1.0f);
  sat.setRowDuration(0.001f); // an absurdly fast tempo (~15000bpm)
  int expectedClamped = static_cast<int>(0.004f * kSampleRate + 0.5f);
  CHECK(sat.getPredelaySamples() == expectedClamped);
}

TEST(haze_predelay_actually_delays_the_signal) {
  // Wide-open band, no drive/bias, so coreOutput_ tracks the input
  // closely enough that a short burst's energy stays localized in time -
  // what's being checked here is the delay's own timing, not the
  // shaper's.
  Haze sat(kSampleRate);
  sat.setParameters(0.0f, SaturatorShape::Tanh, 0.0f, 20.0f, 16000.0f, 0.0f, 0.0f, HazePreDelayDivision::OneOver128, 1.0f);

  int frames = 4096;
  vector<float> input(static_cast<size_t>(frames), 0.0f);
  int burstStart = 200;
  for (int i = burstStart; i < burstStart + 8; i++) input[static_cast<size_t>(i)] = 1.0f;

  sat.process(input.data(), frames);
  vector<float> dry(static_cast<size_t>(frames)), predelayed(static_cast<size_t>(frames));
  sat.getChainSendSum(dry.data(), frames);
  sat.getPredelayedMono(predelayed.data(), frames);

  auto peakIndex = [](const vector<float> & v) {
    size_t best = 0;
    for (size_t i = 1; i < v.size(); i++) if (fabs(v[i]) > fabs(v[best])) best = i;
    return static_cast<int>(best);
  };

  int dryPeak = peakIndex(dry);
  int predelayedPeak = peakIndex(predelayed);
  int observedDelay = predelayedPeak - dryPeak;

  CHECK(observedDelay > 0);
  CHECK(abs(observedDelay - sat.getPredelaySamples()) <= 2); // filter group delay wiggle room
}

TEST(haze_default_preset_matches_compiled_defaults) {
  Haze sat(kSampleRate);
  CHECK(sat.getPreset() == HazePreset::DEFAULT);
}

TEST(haze_unrecognized_preset_text_falls_back_to_default) {
  Haze sat(kSampleRate);
  MemoryParameterSource input;
  input.set("preset", string("not-a-real-preset"));
  sat.loadParameters(input);
  CHECK(sat.getPreset() == HazePreset::DEFAULT);
}

TEST(haze_named_preset_changes_parameters_from_default) {
  Haze defaultSat(kSampleRate);

  Haze sat(kSampleRate);
  MemoryParameterSource input;
  input.set("preset", string("body"));
  sat.loadParameters(input);

  CHECK(sat.getPreset() == HazePreset::BODY);
  // body is tuned to be clearly, deliberately different from the default
  // preset on (at least) shape/diffusion - a loose sanity check that the
  // preset actually took effect, not an exact numeric pin (see
  // bus/Haze.cpp's presetValues() for the authoritative numbers).
  CHECK(sat.getShape() != defaultSat.getShape());
  CHECK(sat.getDiffusion() != defaultSat.getDiffusion());
}

TEST(haze_preset_also_resolves_its_named_shape) {
  Haze sat(kSampleRate);
  MemoryParameterSource input;
  input.set("preset", string("hash"));
  sat.loadParameters(input);

  CHECK(sat.getPreset() == HazePreset::HASH);
  CHECK(sat.getShape() == SaturatorShape::Fold);
}

TEST(haze_explicit_attribute_overrides_preset) {
  Haze sat(kSampleRate);
  MemoryParameterSource input;
  input.set("preset", string("crunch"));
  input.set("diffusion", 0.1f);
  sat.loadParameters(input);

  CHECK(sat.getPreset() == HazePreset::CRUNCH);
  CHECK_NEAR(sat.getDiffusion(), 0.1f, 0.001f);
}

TEST(haze_explicit_shape_attribute_overrides_preset_shape) {
  // A preset implies its own shape (see presetValues()'s SaturatorShape
  // field) the same way it implies drive/bias/hpf/lpf/tilt/predelay/
  // diffusion - an explicit shape="..." attribute must still override it,
  // exactly like every other individually-specified attribute does.
  Haze sat(kSampleRate);
  MemoryParameterSource input;
  input.set("preset", string("hash")); // implies fold
  input.set("shape", string("tanh"));
  sat.loadParameters(input);

  CHECK(sat.getPreset() == HazePreset::HASH);
  CHECK(sat.getShape() == SaturatorShape::Tanh);
}

TEST(haze_preset_alone_round_trips_without_explicit_numeric_attributes) {
  Haze sat(kSampleRate);
  MemoryParameterSource input;
  input.set("preset", string("slap"));
  sat.loadParameters(input);

  MemoryParameterSource output;
  sat.storeParameters(output);

  CHECK(output.getText("preset", "") == "slap");
  CHECK(!output.has("drive"));
  CHECK(!output.has("shape"));
  CHECK(!output.has("bias"));
  CHECK(!output.has("hpf"));
  CHECK(!output.has("lpf"));
  CHECK(!output.has("tilt"));
  CHECK(!output.has("predelay"));
  CHECK(!output.has("diffusion"));
}

TEST(haze_default_preset_round_trips_silently) {
  Haze sat(kSampleRate);

  MemoryParameterSource output;
  sat.storeParameters(output);

  CHECK(!output.has("preset"));
  CHECK(output.isEmpty());
}

TEST(haze_preset_plus_override_round_trips_both) {
  Haze sat(kSampleRate);
  MemoryParameterSource input;
  input.set("preset", string("air"));
  input.set("tilt", 2.5f);
  sat.loadParameters(input);

  MemoryParameterSource output;
  sat.storeParameters(output);

  CHECK(output.getText("preset", "") == "air");
  CHECK_NEAR(output.getFloat("tilt", -100.0f), 2.5f, 0.001f);
  CHECK(!output.has("drive"));
  CHECK(!output.has("shape"));
  CHECK(!output.has("bias"));
  CHECK(!output.has("hpf"));
  CHECK(!output.has("lpf"));
  CHECK(!output.has("predelay"));
  CHECK(!output.has("diffusion"));
}

TEST(haze_trim_always_defaults_to_zero_regardless_of_preset) {
  // trim has no per-preset value (unlike drive/shape/bias/...) - it's
  // always a flat manual offset on top of whatever the preset resolves,
  // so it should never round-trip merely because a preset was chosen.
  Haze sat(kSampleRate);
  MemoryParameterSource input;
  input.set("preset", string("hash"));
  sat.loadParameters(input);

  CHECK_NEAR(sat.getTrim(), 0.0f, 0.001f);

  MemoryParameterSource output;
  sat.storeParameters(output);
  CHECK(!output.has("trim"));
}
