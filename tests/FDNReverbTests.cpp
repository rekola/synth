#include "TestFramework.h"

#include "../bus/FDNReverb.h"
#include "../NoiseGenerator.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Renders `frames` samples of a unit impulse (1.0 at sample 0, silence
// after) through `reverb`, returning each tap's full output concatenated
// per-line so callers can inspect density/decay/correlation.
std::vector<std::vector<float>> renderImpulse(FDNReverb & reverb, int frames) {
  std::vector<float> input(static_cast<size_t>(frames), 0.0f);
  input[0] = 1.0f;

  std::vector<std::vector<float>> taps(static_cast<size_t>(FDNReverb::kNumLines));
  int block = 512;
  for (int offset = 0; offset < frames; offset += block) {
    int n = std::min(block, frames - offset);
    reverb.process(input.data() + offset, n);
    for (int l = 0; l < FDNReverb::kNumLines; l++) {
      auto tap = reverb.getTap(l);
      taps[static_cast<size_t>(l)].insert(taps[static_cast<size_t>(l)].end(), tap, tap + n);
    }
  }
  return taps;
}

// Schroeder backward-integrated energy decay curve (in dB, normalized to
// 0dB at t=0), from the sum of squared samples across every tap - a
// single-line curve would be noisier (each line's own gaps between
// reflections), the aggregate is the physically meaningful "how loud is
// the whole tail" measure RT60 actually describes.
std::vector<float> decayCurveDb(const std::vector<std::vector<float>> & taps) {
  size_t n = taps[0].size();
  std::vector<double> energy(n, 0.0);
  for (auto & tap : taps) {
    for (size_t i = 0; i < n; i++) energy[i] += static_cast<double>(tap[i]) * tap[i];
  }

  std::vector<double> backward(n);
  double sum = 0.0;
  for (size_t i = n; i-- > 0;) {
    sum += energy[i];
    backward[i] = sum;
  }

  double total = backward[0];
  std::vector<float> db(n);
  for (size_t i = 0; i < n; i++) {
    double ratio = total > 0.0 ? backward[i] / total : 0.0;
    db[i] = ratio > 0.0 ? static_cast<float>(10.0 * std::log10(ratio)) : -1000.0f;
  }
  return db;
}

// First sample index where the decay curve crosses -60dB, in seconds -
// -1 if it never does within the rendered length.
float measuredRT60Seconds(const std::vector<float> & db, int sampleRate) {
  for (size_t i = 0; i < db.size(); i++) {
    if (db[i] <= -60.0f) return static_cast<float>(i) / static_cast<float>(sampleRate);
  }
  return -1.0f;
}

}

TEST(fdn_reverb_impulse_produces_dense_decaying_tail_on_every_tap) {
  FDNReverb reverb(44100);
  auto taps = renderImpulse(reverb, 44100); // 1 second

  for (auto & tap : taps) {
    int nonzero = 0;
    for (auto v : tap) if (v != 0.0f) nonzero++;
    // Dense: not just the handful of samples an unspread impulse/direct
    // echo would produce - most of a 1-second render should carry tail
    // energy given the ~1.8s default RT60.
    CHECK(nonzero > 10000);
  }

  // Smoothly decaying, not silent - checked on the aggregate (summed
  // across all 8 taps), not per-tap: the longest-delay tap's own first
  // echo doesn't arrive until pre-delay + diffusion + its own ~83ms base
  // delay, comfortably past a 100ms per-tap early window, so a per-tap
  // early-vs-late comparison would spuriously fail for that one tap even
  // though the network as a whole is decaying correctly (confirmed by
  // measuring each tap's raw early/late energy while debugging this test).
  double early = 0.0, late = 0.0;
  for (auto & tap : taps) {
    for (int i = 0; i < 4410; i++) early += static_cast<double>(tap[static_cast<size_t>(i)]) * tap[static_cast<size_t>(i)];
    for (int i = 44100 - 4410; i < 44100; i++) late += static_cast<double>(tap[static_cast<size_t>(i)]) * tap[static_cast<size_t>(i)];
  }
  CHECK(early > 0.0);
  CHECK(late < early);
}

TEST(fdn_reverb_stable_across_parameter_extremes) {
  // Several (size, decay, damping, predelay) combinations spanning the
  // full supported range, including both extremes - every rendered sample
  // must stay finite and bounded (no runaway feedback), and the tail must
  // eventually decay well below its own peak, confirming feedbackGain < 1
  // holds in practice even where FDNReverb.cpp's own assert is compiled
  // out (NDEBUG).
  //
  // 2 seconds, not a first-half/second-half split of a short render: at
  // size=3.0 combined with a 0.2s pre-delay, the longest line's first
  // echo doesn't arrive until predelay + diffusion + ~249ms (83ms base *
  // 3.0 size) - comfortably past an earlier, shorter render's halfway
  // point, which made a "second half quieter than first half" comparison
  // spuriously fail on exactly this kind of extreme (confirmed while
  // debugging this test: energy was ~1e-36 in the first half and genuinely
  // decaying-from-onset in the second). Comparing the peak windowed energy
  // anywhered in the render against the final window instead is robust to
  // exactly where the tail actually starts.
  struct Case { float size, decay, damping, predelay; };
  Case cases[] = {
    { 0.1f, 0.01f, 0.0f, 0.0f },
    { 3.0f, 0.01f, 0.0f, 0.0f },
    { 0.1f, 10.0f, 1.0f, 0.2f },
    { 3.0f, 10.0f, 1.0f, 0.2f },
    { 1.0f, 1.8f, 0.1f, 0.02f },
  };

  int sampleRate = 44100;
  int frames = sampleRate * 2;
  int window = 4410; // 100ms

  for (auto & c : cases) {
    FDNReverb reverb(sampleRate);
    reverb.setParameters(c.size, c.decay, c.damping, c.predelay);
    auto taps = renderImpulse(reverb, frames);

    std::vector<double> window_energy(static_cast<size_t>(frames / window), 0.0);
    for (auto & tap : taps) {
      for (int i = 0; i < frames; i++) {
	float v = tap[static_cast<size_t>(i)];
	CHECK(std::isfinite(v));
	CHECK(std::fabs(v) < 100.0f); // generous bound - a stable network never gets near this
	window_energy[static_cast<size_t>(i / window)] += static_cast<double>(v) * v;
      }
    }

    double peak = *std::max_element(window_energy.begin(), window_energy.end());
    double final_window = window_energy.back();
    CHECK(peak > 0.0);
    CHECK(final_window < peak * 0.1);
  }
}

TEST(fdn_reverb_rt60_matches_requested_decay_within_tolerance) {
  // FDN aggregate decay measurement is inherently noisier than a single
  // exponential (8 lines with different delay lengths, each individually
  // tuned to the same target RT60 but summing into one aggregate envelope
  // whose backward-integrated -60dB crossing doesn't exactly equal any
  // single line's own RT60 - confirmed empirically while writing this
  // test: the measured value consistently runs somewhat high), so the
  // tolerance here is loose (50%) - it's a sanity check that the decay
  // parameter actually controls the tail length in the right direction
  // and roughly the right magnitude, not a precision claim.
  int sampleRate = 44100;

  FDNReverb short_reverb(sampleRate);
  short_reverb.setParameters(1.0f, 0.6f, 0.0f, 0.0f); // no damping - measure the broadband/DC decay only
  auto short_taps = renderImpulse(short_reverb, sampleRate * 2);
  auto short_rt60 = measuredRT60Seconds(decayCurveDb(short_taps), sampleRate);

  FDNReverb long_reverb(sampleRate);
  long_reverb.setParameters(1.0f, 1.8f, 0.0f, 0.0f);
  auto long_taps = renderImpulse(long_reverb, sampleRate * 3);
  auto long_rt60 = measuredRT60Seconds(decayCurveDb(long_taps), sampleRate);

  CHECK(short_rt60 > 0.0f);
  CHECK(long_rt60 > 0.0f);
  CHECK_NEAR(short_rt60, 0.6f, 0.6f * 0.5f);
  CHECK_NEAR(long_rt60, 1.8f, 1.8f * 0.5f);
  CHECK(long_rt60 > short_rt60); // decay parameter's direction is unambiguous either way
}

TEST(fdn_reverb_taps_are_mutually_decorrelated) {
  FDNReverb reverb(44100);

  int frames = 22050; // 0.5s noise burst
  std::vector<float> input(static_cast<size_t>(frames));
  NoiseGenerator noise(12345);
  for (auto & v : input) v = noise.next();

  std::vector<std::vector<float>> taps(static_cast<size_t>(FDNReverb::kNumLines));
  int block = 512;
  for (int offset = 0; offset < frames; offset += block) {
    int n = std::min(block, frames - offset);
    reverb.process(input.data() + offset, n);
    for (int l = 0; l < FDNReverb::kNumLines; l++) {
      auto tap = reverb.getTap(l);
      taps[static_cast<size_t>(l)].insert(taps[static_cast<size_t>(l)].end(), tap, tap + n);
    }
  }

  // Pairwise normalized cross-correlation (zero-lag) between a handful of
  // tap pairs - well below 1 confirms the alternating-sign injection/
  // Householder mix actually decorrelates the taps, not just scales a
  // single shared signal across all 8.
  auto correlation = [&](int a, int b) {
    double num = 0.0, ea = 0.0, eb = 0.0;
    auto & ta = taps[static_cast<size_t>(a)];
    auto & tb = taps[static_cast<size_t>(b)];
    for (size_t i = 0; i < ta.size(); i++) {
      num += static_cast<double>(ta[i]) * tb[i];
      ea += static_cast<double>(ta[i]) * ta[i];
      eb += static_cast<double>(tb[i]) * tb[i];
    }
    return num / std::sqrt(ea * eb);
  };

  CHECK(std::fabs(correlation(0, 1)) < 0.9);
  CHECK(std::fabs(correlation(0, 4)) < 0.9);
  CHECK(std::fabs(correlation(2, 6)) < 0.9);
}

TEST(fdn_reverb_size_change_mid_stream_has_no_large_sample_jump) {
  FDNReverb reverb(44100);

  int frames = 8820; // 0.2s of noise to fill the network with real content first
  std::vector<float> input(static_cast<size_t>(frames));
  NoiseGenerator noise(777);
  for (auto & v : input) v = noise.next();

  int block = 256;
  float steady_state_peak = 0.0f;
  for (int offset = 0; offset < frames; offset += block) {
    int n = std::min(block, frames - offset);
    reverb.process(input.data() + offset, n);
    if (offset + n >= frames) {
      for (int l = 0; l < FDNReverb::kNumLines; l++) {
	auto tap = reverb.getTap(l);
	for (int i = 0; i < n; i++) steady_state_peak = std::max(steady_state_peak, std::fabs(tap[i]));
      }
    }
  }
  CHECK(steady_state_peak > 0.05f); // the network is genuinely excited before the change

  // Change size mid-stream (simulating a song change): the read-offset
  // jump this causes must be masked by process()'s fade, not heard as a
  // discontinuity. Broadband noise content itself has large legitimate
  // sample-to-sample deltas (not a glitch), so this checks the actual
  // masking mechanism directly instead of an arbitrary max-delta bound:
  // the fade starts at gain 0 the instant setParameters() changes size
  // (see FDNReverb.cpp), so the very first samples afterward must be
  // small relative to the steady-state level just observed, then ramp
  // back up smoothly over kSizeChangeFadeMs - not snap straight to a new,
  // uncorrelated buffer region at full amplitude.
  reverb.setParameters(2.5f, 1.8f, 0.1f, 0.02f);

  std::vector<float> silence(4, 0.0f);
  reverb.process(silence.data(), 4);
  for (int l = 0; l < FDNReverb::kNumLines; l++) {
    auto tap = reverb.getTap(l);
    CHECK(std::fabs(tap[0]) < steady_state_peak * 0.2f);
  }

  // A few ms later (fade still in progress, since kSizeChangeFadeMs is
  // 10ms), amplitude should have grown back up, not still be pinned at 0 -
  // confirms it's a ramp, not a permanent mute.
  std::vector<float> silence_block(200, 0.0f);
  reverb.process(silence_block.data(), 200);
  float ramping_peak = 0.0f;
  for (int l = 0; l < FDNReverb::kNumLines; l++) {
    auto tap = reverb.getTap(l);
    for (int i = 0; i < 200; i++) ramping_peak = std::max(ramping_peak, std::fabs(tap[i]));
  }
  CHECK(ramping_peak > 0.0f);
}
