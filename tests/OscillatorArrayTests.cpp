#include "TestFramework.h"

#include "../src/instruments/Oscillator.h"
#include "../src/instruments/OscillatorKernel.h"
#include "../src/instruments/OscillatorVoice.h"
#include "../src/instruments/PitchDrift.h"
#include "../src/state/MemoryParameterSource.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/SphericalPosition.h"

#include <cmath>
#include <vector>

using namespace std;

namespace {

constexpr double kPi = 3.14159265358979323846;

// The per-sample waveforms as OscillatorVoice computes them, in double.
double reference(WaveformType type, double phase, double pulse_width) {
  double f = phase - floor(phase);
  switch (type) {
  case WaveformType::SINE: return sin(2.0 * kPi * f);
  case WaveformType::SAW: return f < 0.5 ? 2.0 * f : 2.0 * f - 2.0;
  case WaveformType::TRIANGLE: return f < 0.5 ? 1.0 - 4.0 * f : 4.0 * f - 3.0;
  case WaveformType::SQUARE: return f < pulse_width ? -1.0 : 1.0;
  default: return 0.0;
  }
}

} // namespace

TEST(oscillator_kernel_matches_the_scalar_waveforms) {
  const double rate = 440.0 / 44100.0 * 1.5;
  const int frames = 1003; // not a multiple of the vector width
  const WaveformType types[] = { WaveformType::SINE, WaveformType::SAW, WaveformType::TRIANGLE, WaveformType::SQUARE };

  for (auto type : types) {
    vector<float> out(oscillator_kernel::paddedFrames(frames));
    int mismatches = 0;
    double max_err = 0.0;
    double phase = 0.37;
    // Several blocks, so the phase carry between them is exercised too.
    for (int block = 0; block < 20; block++) {
      fill(out.begin(), out.end(), 0.0f);
      oscillator_kernel::mix(type, 0.3f, phase, rate, 0.5f, frames, out.data());
      for (int k = 0; k < frames; k++) {
	double expected = 0.5 * reference(type, phase + rate * k, 0.3);
	double err = fabs(out[static_cast<size_t>(k)] - expected);
	// A discontinuity can land one sample either side of the exact edge.
	if (err > 1e-4) mismatches++; else max_err = max(max_err, err);
      }
      phase += rate * frames;
      phase -= floor(phase);
    }
    CHECK(mismatches < 20 * frames / 1000);
    CHECK(max_err < 1e-4);

    // mix() adds to what is already there.
    vector<float> twice(oscillator_kernel::paddedFrames(frames), 1.0f);
    oscillator_kernel::mix(type, 0.3f, 0.2, rate, 0.5f, frames, twice.data());
    vector<float> once(oscillator_kernel::paddedFrames(frames), 0.0f);
    oscillator_kernel::mix(type, 0.3f, 0.2, rate, 0.5f, frames, once.data());
    for (int k = 0; k < frames; k++) CHECK_NEAR(twice[static_cast<size_t>(k)], once[static_cast<size_t>(k)] + 1.0f, 1e-6f);
  }
}

TEST(oscillator_kernel_sine_polynomial_is_accurate) {
  const int frames = 3000;
  vector<float> out(oscillator_kernel::paddedFrames(frames), 0.0f);
  oscillator_kernel::mix(WaveformType::SINE, 0.5f, 0.0, 1.0 / 1000.0, 1.0f, frames, out.data());
  double max_err = 0.0;
  for (int i = 0; i < frames; i++) max_err = max(max_err, fabs(static_cast<double>(out[static_cast<size_t>(i)]) - sin(2.0 * kPi * i / 1000.0)));
  CHECK(max_err < 2e-6);
}

// An array voice must sound like the same members played as separate
// single-member voices, each at its bucket's direction (floor reflection off,
// so no per-member/summed difference).
TEST(oscillator_array_matches_separate_voices) {
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  SendLevels sends;
  NoteCoordinate coord(1, 16, 0);

  OscillatorArray array;
  array.voices = 4;
  array.ratio = 1.5f;
  array.falloff = 0.5f;
  array.detune_cents = 12.0f;
  array.spread = 1.0f;

  SphericalPosition centre;
  centre.distance = 2.0f;
  centre.extent = 3.0f;

  const float frequency = 330.0f, velocity = 0.8f, note_detune = 1.01f;
  OscillatorVoice multi(config, centre, note_detune, WaveformType::SAW, 0.7f, 0.5f, sends, coord, array);
  CHECK(multi.memberCount() == 4);
  CHECK(multi.bucketCount() > 1); // the members really are spread out
  multi.playNote(frequency, velocity, 60);

  vector<unique_ptr<OscillatorVoice>> separate;
  for (int k = 0; k < 4; k++) {
    float place = 2.0f * static_cast<float>(k) / 3.0f - 1.0f;
    float ratio = note_detune * powf(array.ratio, static_cast<float>(k)) * powf(2.0f, place * array.detune_cents / 2400.0f);
    auto voice = make_unique<OscillatorVoice>(config, multi.bucketDirection(multi.bucketOf(static_cast<size_t>(k))), ratio, WaveformType::SAW, 0.7f * powf(array.falloff, static_cast<float>(k)), 0.5f, sends, coord.withInstance(k));
    voice->playNote(frequency, velocity, 60);
    separate.push_back(move(voice));
  }

  int bad = 0, total = 0;
  double energy = 0.0;
  for (int block = 0; block < 10; block++) {
    const int frames = 512;
    auto a = multi.render(frames);
    AudioBuffer expected(config.numberOfChannels(), frames);
    expected.zero();
    for (auto & v : separate) expected.mixNamed(v->render(frames));

    CHECK(a.regularChannelCount() == config.numberOfChannels());
    for (int c = 0; c < a.regularChannelCount(); c++) {
      for (int k = 0; k < frames; k++, total++) {
	energy += static_cast<double>(a.getChannelData(c)[k]) * static_cast<double>(a.getChannelData(c)[k]);
	if (fabsf(a.getChannelData(c)[k] - expected.getChannelData(c)[k]) > 1e-3f) bad++;
      }
    }
  }
  CHECK(energy > 1.0);
  CHECK(bad < total / 500); // only a saw's wrap sample may differ
}

// Members sharing a direction (no spread) are summed and encoded once; the
// result must still equal the same members played as separate voices.
TEST(oscillator_array_without_spread_encodes_once_and_matches_separate_voices) {
  ChannelConfiguration config(44100, 3);
  config.setFloorReflectionEnabled(true);
  SendLevels sends;
  sends.a = 0.3f;
  NoteCoordinate coord(2, 8, 0);

  OscillatorArray array;
  array.voices = 8;
  array.detune_cents = 14.0f;
  array.falloff = 0.9f;

  SphericalPosition centre;
  centre.azimuth = 25.0f;
  centre.distance = 3.0f;
  centre.extent = 2.0f;

  OscillatorVoice multi(config, centre, 1.0f, WaveformType::TRIANGLE, 0.5f, 0.5f, sends, coord, array);
  CHECK(multi.memberCount() == 8);
  CHECK(multi.bucketCount() == 1);
  multi.playNote(262.0f, 0.7f, 60);

  // Floor reflection is on: the separate voices each reflect their own
  // copy, the array reflects the sum - linear, so the same total.
  vector<unique_ptr<OscillatorVoice>> separate;
  for (int k = 0; k < 8; k++) {
    float place = 2.0f * static_cast<float>(k) / 7.0f - 1.0f;
    float ratio = powf(2.0f, place * array.detune_cents / 2400.0f);
    auto voice = make_unique<OscillatorVoice>(config, centre, ratio, WaveformType::TRIANGLE, 0.5f * powf(array.falloff, static_cast<float>(k)), 0.5f, sends, coord.withInstance(k));
    voice->playNote(262.0f, 0.7f, 60);
    separate.push_back(move(voice));
  }

  double err = 0.0, energy = 0.0;
  for (int block = 0; block < 12; block++) {
    const int frames = 256;
    auto a = multi.render(frames);
    AudioBuffer expected(config.numberOfChannels(), true, false, frames);
    expected.zero();
    for (auto & v : separate) expected.mixNamed(v->render(frames));

    for (int c = 0; c < a.regularChannelCount(); c++) {
      for (int k = 0; k < frames; k++) {
	double d = static_cast<double>(a.getChannelData(c)[k]) - static_cast<double>(expected.getChannelData(c)[k]);
	err += d * d;
	energy += static_cast<double>(expected.getChannelData(c)[k]) * static_cast<double>(expected.getChannelData(c)[k]);
      }
    }
    auto * a_aux = a.getChannel(Channel::AuxA);
    auto * e_aux = expected.getChannel(Channel::AuxA);
    CHECK(a_aux != nullptr && e_aux != nullptr);
    for (int k = 0; k < frames; k++) CHECK_NEAR(a_aux[k], e_aux[k], 2e-4f);
  }
  CHECK(energy > 1.0);
  CHECK(err < energy * 1e-6);
}

// A array gets as many buckets as resolvable cells fit in its cloud (at most
// one per member, at least three): a wide spread gets many, a narrow one
// three, a cloud under one cell or no spread a single bucket.
TEST(oscillator_array_bucket_count_follows_the_cloud_area_and_the_order) {
  auto buckets = [](int order, int voices, float spread, float extent, float distance) {
    ChannelConfiguration config(44100, order);
    OscillatorArray array;
    array.voices = voices;
    array.spread = spread;
    SphericalPosition centre;
    centre.distance = distance;
    centre.extent = extent;
    OscillatorVoice voice(config, centre, 1.0f, WaveformType::SINE, 1.0f, 0.5f, SendLevels{}, NoteCoordinate(0, 0, 0), array);
    return voice.bucketCount();
  };

  // A +-45 degree cloud: about 4 cells of 25 degrees at order 1, 11 of 16
  // at order 2, 19 of 12 at order 3.
  CHECK(buckets(1, 32, 1.0f, 1.0f, 1.0f) == 4);
  CHECK(buckets(2, 32, 1.0f, 1.0f, 1.0f) == 11);
  CHECK(buckets(3, 32, 1.0f, 1.0f, 1.0f) == 19);
  // Never more buckets than members.
  CHECK(buckets(3, 3, 1.0f, 1.0f, 1.0f) == 3);
  CHECK(buckets(3, 256, 180.0f, 1.0f, 1.0f) <= 256);
  // No spread, no extent, a mono bus or no distance: one.
  CHECK(buckets(3, 32, 0.0f, 1.0f, 1.0f) == 1);
  CHECK(buckets(3, 32, 1.0f, 0.0f, 1.0f) == 1);
  // A cloud smaller than one cell is one bucket; any wider is at least a
  // triangle, so it is two-dimensional.
  CHECK(buckets(3, 32, 0.01f, 1.0f, 1.0f) == 1);
  CHECK(buckets(3, 32, 0.1f, 1.0f, 1.0f) == 1);
  CHECK(buckets(3, 32, 0.2f, 1.0f, 1.0f) == 3);
  CHECK(buckets(1, 32, 0.5f, 1.0f, 1.0f) == 3);
  CHECK(buckets(3, 2, 1.0f, 1.0f, 1.0f) == 2);
  CHECK(buckets(0, 32, 1.0f, 1.0f, 1.0f) == 1);
  CHECK(buckets(3, 32, 1.0f, 1.0f, 0.0f) == 1);
}

// Ring counts: ceil((sqrt(1 + 4B/3) - 1) / 2) rings, populations
// proportional to radius by cumulative rounding, always summing to B.
TEST(oscillator_array_ring_counts_are_concentric_and_sum_to_the_bucket_count) {
  auto rings = [](int b) { return OscillatorVoice::ringCounts(b); };
  CHECK(rings(1).empty());
  CHECK((rings(2) == vector<int>{ 2 }));
  CHECK((rings(3) == vector<int>{ 3 }));
  CHECK((rings(6) == vector<int>{ 6 }));
  CHECK((rings(7) == vector<int>{ 2, 5 }));
  CHECK((rings(8) == vector<int>{ 3, 5 }));
  CHECK((rings(19) == vector<int>{ 3, 7, 9 }));

  for (int b = 2; b <= 256; b++) {
    auto counts = rings(b);
    int sum = 0;
    for (int n : counts) { CHECK(n >= 1); sum += n; }
    CHECK(sum == b);
    // Outer rings never hold fewer points than inner ones.
    for (size_t j = 1; j < counts.size(); j++) CHECK(counts[j] >= counts[j - 1]);
  }
}

// The layout: a single bucket at the centre, two left and right, three a
// triangle apex up, everything inside the ellipse and nothing coincident.
TEST(oscillator_array_cloud_points_form_concentric_rings) {
  SphericalPosition centre;
  centre.azimuth = 10.0f;
  centre.elevation = 5.0f;
  const float r = 30.0f, r_el = r / kExtentShapeRatio;
  auto point = [&](int b, int count) { return OscillatorVoice::cloudPoint(centre, r, b, OscillatorVoice::ringCounts(count)); };

  CHECK_NEAR(point(0, 1).azimuth, 10.0f, 1e-4f);
  CHECK_NEAR(point(0, 1).elevation, 5.0f, 1e-4f);

  // Two: left and right at the same elevation.
  CHECK_NEAR(point(0, 2).azimuth, 10.0f - r, 1e-3f);
  CHECK_NEAR(point(1, 2).azimuth, 10.0f + r, 1e-3f);
  CHECK_NEAR(point(0, 2).elevation, 5.0f, 1e-3f);
  CHECK_NEAR(point(1, 2).elevation, 5.0f, 1e-3f);

  // Three: apex up, the other two level and symmetric below it.
  CHECK_NEAR(point(0, 3).azimuth, 10.0f, 1e-3f);
  CHECK_NEAR(point(0, 3).elevation, 5.0f + r_el, 1e-3f);
  CHECK(point(1, 3).elevation < 5.0f);
  CHECK_NEAR(point(1, 3).elevation, point(2, 3).elevation, 1e-3f);
  CHECK_NEAR(point(1, 3).azimuth - 10.0f, 10.0f - point(2, 3).azimuth, 1e-3f);

  for (int count = 2; count <= 40; count++) {
    for (int b = 0; b < count; b++) {
      auto p = point(b, count);
      float x = (p.azimuth - 10.0f) / r, y = (p.elevation - 5.0f) / r_el;
      CHECK(x * x + y * y <= 1.0001f); // inside the ellipse
      for (int c = b + 1; c < count; c++) {
	auto q = point(c, count);
	CHECK(fabsf(p.azimuth - q.azimuth) + fabsf(p.elevation - q.elevation) > 0.01f);
      }
    }
  }
  // Seven: two rings, so points at two distinct radii.
  float inner = hypotf((point(0, 7).azimuth - 10.0f) / r, (point(0, 7).elevation - 5.0f) / r_el);
  float outer = hypotf((point(6, 7).azimuth - 10.0f) / r, (point(6, 7).elevation - 5.0f) / r_el);
  CHECK_NEAR(inner, 0.5f, 1e-3f);
  CHECK_NEAR(outer, 1.0f, 1e-3f);
}

// Members are dealt round-robin: buckets stay evenly filled, and members
// neighbouring in pitch land in different buckets.
TEST(oscillator_array_members_are_dealt_round_robin_into_buckets) {
  ChannelConfiguration config(44100, 3);
  OscillatorArray array;
  array.voices = 50;
  array.spread = 1.0f;
  SphericalPosition centre;
  centre.distance = 1.0f;
  centre.extent = 1.0f;
  OscillatorVoice voice(config, centre, 1.0f, WaveformType::SINE, 1.0f, 0.5f, SendLevels{}, NoteCoordinate(0, 0, 0), array);

  const size_t buckets = voice.bucketCount();
  CHECK(buckets > 1);
  size_t total = 0, smallest = 1000, largest = 0;
  for (size_t b = 0; b < buckets; b++) {
    total += voice.bucketSize(b);
    smallest = min(smallest, voice.bucketSize(b));
    largest = max(largest, voice.bucketSize(b));
  }
  CHECK(total == 50);
  CHECK(largest - smallest <= 1);
  for (size_t k = 0; k + 1 < 50; k++) CHECK(voice.bucketOf(k) != voice.bucketOf(k + 1));
}

// A large array with spread renders finite, audible output.
TEST(oscillator_array_with_a_wide_spread_renders) {
  ChannelConfiguration config(44100, 3);
  OscillatorArray array;
  array.voices = 32;
  array.detune_cents = 20.0f;
  array.spread = 1.0f;
  SphericalPosition centre;
  centre.distance = 1.0f;
  centre.extent = 1.0f;
  OscillatorVoice voice(config, centre, 1.0f, WaveformType::SAW, 0.2f, 0.5f, SendLevels{}, NoteCoordinate(3, 24, 0), array);
  voice.playNote(196.0f, 0.7f, 55);

  double energy = 0.0;
  for (int block = 0; block < 8; block++) {
    auto out = voice.render(512);
    for (int c = 0; c < out.regularChannelCount(); c++) {
      for (int k = 0; k < 512; k++) {
	float v = out.getChannelData(c)[k];
	CHECK(std::isfinite(v));
	energy += static_cast<double>(v) * static_cast<double>(v);
      }
    }
  }
  CHECK(energy > 1.0);
}

// The default array is one member, rendered exactly like a plain voice.
TEST(oscillator_default_array_is_a_single_member) {
  Oscillator osc(WaveformType::SQUARE);
  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;

  auto voice = osc.playNote(config, position, Tuning::EDO31, 1.0f, 1.0f, 60, SendLevels{}, NoteCoordinate(0, 0, 0));
  CHECK(voice.get() != nullptr);
  CHECK(voice->getAllocatedVoiceCount() == 1);
}

TEST(oscillator_voices_attribute_builds_one_array_voice) {
  Oscillator osc(WaveformType::SAW);
  MemoryParameterSource params;
  params.set("type", string("saw"));
  params.set("voices", 8);
  params.set("detune", 10.0f);
  params.set("spread", 1.0f);
  osc.loadParameters(params);

  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  position.extent = 1.0f;

  auto voice = osc.playNote(config, position, Tuning::EDO31, 1.0f, 1.0f, 60, SendLevels{}, NoteCoordinate(0, 0, 0));
  CHECK(voice->getAllocatedVoiceCount() == 1); // one voice, however many members

  bool any = false;
  for (int block = 0; block < 4; block++) {
    auto out = voice->render(256);
    CHECK(out.hasChannel(Channel::Main));
    for (int c = 0; c < out.regularChannelCount(); c++) {
      for (int k = 0; k < 256; k++) {
	float v = out.getChannelData(c)[k];
	CHECK(std::isfinite(v));
	any = any || v != 0.0f;
      }
    }
  }
  CHECK(any);

  // Round-trips through the parameters, defaults omitted.
  MemoryParameterSource stored;
  osc.storeParameters(stored);
  CHECK(stored.get<int>("voices", 1) == 8);
  CHECK_NEAR(stored.get<float>("detune", 0.0f), 10.0f, 1e-6f);
}

// ---- Pitch drift ----

namespace {

constexpr double kCentsToRatio = 0.69314718055994531 / 1200.0;

// The W channel of a drifting (period 0: still) array, rendered in blocks of the given
// sizes (repeated), `total` samples in all.
vector<float> renderW(float drift_period, int voices, float detune_cents, vector<int> block_sizes, int total, float frequency = 440.0f) {
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  OscillatorArray array;
  array.voices = voices;
  array.detune_cents = detune_cents;
  array.drift_period = drift_period;
  SphericalPosition position;
  position.distance = 1.0f;
  OscillatorVoice voice(config, position, 1.0f, WaveformType::SINE, 0.5f, 0.5f, SendLevels{}, NoteCoordinate(3, 5, 0), array);
  voice.playNote(frequency, 1.0f, 60);

  vector<float> out;
  size_t which = 0;
  while (static_cast<int>(out.size()) < total) {
    const int frames = min(block_sizes[which++ % block_sizes.size()], total - static_cast<int>(out.size()));
    auto buffer = voice.render(frames);
    out.insert(out.end(), buffer.getChannelData(0), buffer.getChannelData(0) + frames);
  }
  return out;
}

}

TEST(pitch_drift_starts_on_pitch_and_stays_within_its_depth) {
  const double period = 8820.0;
  PitchDriftClock clock(period);
  PitchDriftMember member(12345, 10.0f);
  auto deviation = [&](uint64_t t) { return member.deviation(clock.at(t)); };
  const double limit = 10.0 * kCentsToRatio;
  CHECK(deviation(0) == 0.0);
  CHECK(member.integral(clock.at(0)) == 0.0);

  double max_dev = 0.0, sum = 0.0, sum_sq = 0.0;
  const int n = 4000;
  for (int i = 0; i < n; i++) {
    double d = deviation(static_cast<uint64_t>(i) * 2205 + 1);
    max_dev = max(max_dev, fabs(d));
    sum += d;
    sum_sq += d * d;
  }
  CHECK(max_dev <= limit * 1.0001);
  CHECK(max_dev > limit * 0.8); // and really uses the range
  const double mean = sum / n;
  const double stddev = sqrt(sum_sq / n - mean * mean);
  CHECK(stddev > 0.25 * limit);
  CHECK(stddev < 0.65 * limit);
  CHECK(fabs(mean) < 0.1 * limit);
}

TEST(pitch_drift_integral_is_the_running_sum_of_the_deviation) {
  PitchDriftClock clock(4410.0);
  PitchDriftMember member(7, 20.0f);
  auto deviation = [&](uint64_t t) { return member.deviation(clock.at(t)); };
  auto integral = [&](uint64_t t) { return member.integral(clock.at(t)); };
  double sum = 0.0;
  double worst = 0.0;
  for (uint64_t t = 0; t < 40000; t++) {
    // Trapezoid rule: the sum over [0, t) corrected at its ends, against integral(t).
    if (t % 997 == 0) worst = max(worst, fabs(integral(t) - (sum + 0.5 * (deviation(t) - deviation(0)))));
    sum += deviation(t);
  }
  CHECK(worst < 1e-3);
}

TEST(pitch_drift_depends_only_on_time_seed_and_period) {
  PitchDriftClock clock(5000.0);
  PitchDriftMember forward(99, 15.0f), jumpy(99, 15.0f), other(100, 15.0f);
  vector<double> a;
  for (uint64_t t = 0; t < 60000; t += 777) a.push_back(forward.deviation(clock.at(t)));

  // Any query order gives the same values (the segment cache is only a cache).
  size_t k = a.size();
  for (uint64_t t = 60000; t > 0; t -= 777 > t ? t : 777) {
    k--;
    uint64_t at = (t - 1) / 777 * 777;
    CHECK(fabs(jumpy.deviation(clock.at(at)) - a[at / 777]) < 1e-12);
  }

  // A different seed gives a different wander, uncorrelated with the first.
  double ab = 0.0, aa = 0.0, bb = 0.0;
  for (uint64_t t = 0; t < 400000; t += 311) {
    const PitchDriftPoint point = clock.at(t);
    double x = forward.deviation(point), y = other.deviation(point);
    ab += x * y;
    aa += x * x;
    bb += y * y;
  }
  CHECK(fabs(ab / sqrt(aa * bb)) < 0.2);
}

TEST(pitch_drift_off_by_default_and_round_trips_through_parameters) {
  Oscillator plain(WaveformType::SINE);
  MemoryParameterSource none;
  plain.loadParameters(none);
  MemoryParameterSource stored_none;
  plain.storeParameters(stored_none);
  CHECK(stored_none.get<float>("driftPeriod", -1.0f) == -1.0f);

  Oscillator osc(WaveformType::SINE);
  MemoryParameterSource params;
  params.set("voices", 4);
  params.set("detune", 8.0f);
  params.set("driftPeriod", 0.5f);
  osc.loadParameters(params);
  MemoryParameterSource stored;
  osc.storeParameters(stored);
  CHECK_NEAR(stored.get<float>("driftPeriod", 0.0f), 0.5f, 1e-6f);
}

// The trajectory is a function of the voice's age alone: cutting the same
// notes into different blocks (even randomly) gives the same signal.
TEST(oscillator_drift_does_not_depend_on_the_block_size) {
  const int total = 44100;
  auto base = renderW(0.2f, 3, 8.0f, { 256 }, total);
  auto odd = renderW(0.2f, 3, 8.0f, { 37, 101, 8, 513, 1 }, total);
  auto big = renderW(0.2f, 3, 8.0f, { 4096 }, total);
  auto still = renderW(0.0f, 3, 8.0f, { 256 }, total);

  double worst_odd = 0.0, worst_big = 0.0, moved = 0.0;
  for (size_t i = 0; i < base.size(); i++) {
    worst_odd = max(worst_odd, static_cast<double>(fabsf(base[i] - odd[i])));
    worst_big = max(worst_big, static_cast<double>(fabsf(base[i] - big[i])));
    moved = max(moved, static_cast<double>(fabsf(base[i] - still[i])));
  }
  CHECK(worst_odd < 1e-3);
  CHECK(worst_big < 1e-3);
  CHECK(moved > 0.1); // the drift really changed the signal
}

TEST(oscillator_drift_is_deterministic_and_has_no_clicks) {
  auto a = renderW(0.5f, 4, 6.0f, { 256 }, 22050);
  auto b = renderW(0.5f, 4, 6.0f, { 256 }, 22050);
  CHECK(a == b);

  // A sustained sine array never steps: sample-to-sample change stays small.
  float worst = 0.0f;
  for (size_t i = 1; i < a.size(); i++) worst = max(worst, fabsf(a[i] - a[i - 1]));
  CHECK(worst < 0.1f);
}

// Two detuned members beat at a fixed rate, a cycle the ear learns; drift
// must break that cycle.
TEST(oscillator_drift_breaks_the_static_beat_cycle) {
  const int total = 44100 * 12;
  const int window = 882; // 20 ms
  auto envelope = [&](const vector<float> & signal) {
    vector<double> env;
    for (size_t at = 0; at + window <= signal.size(); at += window) {
      double e = 0.0;
      for (int k = 0; k < window; k++) e += static_cast<double>(signal[at + static_cast<size_t>(k)]) * static_cast<double>(signal[at + static_cast<size_t>(k)]);
      env.push_back(sqrt(e / window));
    }
    return env;
  };
  // Peak of the normalised envelope autocorrelation near the beat period.
  auto beat_correlation = [](const vector<double> & env, int lo, int hi) {
    double mean = 0.0;
    for (double v : env) mean += v;
    mean /= static_cast<double>(env.size());
    double best = -1.0, var = 0.0;
    for (double v : env) var += (v - mean) * (v - mean);
    for (int lag = lo; lag <= hi; lag++) {
      double c = 0.0;
      for (size_t i = 0; i + static_cast<size_t>(lag) < env.size(); i++) c += (env[i] - mean) * (env[i + static_cast<size_t>(lag)] - mean);
      best = max(best, c / var);
    }
    return best;
  };

  // 440 Hz members 6 cents apart beat at about 1.5 Hz, a period of ~33 windows.
  auto fixed = envelope(renderW(0.0f, 2, 6.0f, { 256 }, total));
  auto wander = envelope(renderW(0.5f, 2, 6.0f, { 256 }, total));
  const double still = beat_correlation(fixed, 30, 36);
  const double drifting = beat_correlation(wander, 30, 36);
  CHECK(still > 0.8);
  CHECK(drifting < still - 0.3);
}

// Every note gets its own turn and jitter of the ring layout, the same note
// always the same one, and the points stay inside the cloud and apart.
TEST(oscillator_array_scatters_the_cloud_per_note) {
  SphericalPosition centre;
  centre.azimuth = 10.0f;
  centre.elevation = 5.0f;
  const float radius = 40.0f;

  for (int count : { 3, 7, 12, 19, 40 }) {
    const vector<int> rings = OscillatorVoice::ringCounts(count);
    int64_t coord_a = 111, coord_b = 222;
    int same = 0, different = 0;
    float closest = 1e9f;
    for (int b = 0; b < count; b++) {
      auto a1 = OscillatorVoice::cloudPoint(centre, radius, b, rings, &coord_a);
      auto a2 = OscillatorVoice::cloudPoint(centre, radius, b, rings, &coord_a);
      auto other = OscillatorVoice::cloudPoint(centre, radius, b, rings, &coord_b);
      if (a1.azimuth == a2.azimuth && a1.elevation == a2.elevation) same++;
      if (fabsf(a1.azimuth - other.azimuth) > 1e-3f || fabsf(a1.elevation - other.elevation) > 1e-3f) different++;

      // Inside the ellipse (azimuth radius r, elevation radius r / 3).
      const float x = (a1.azimuth - centre.azimuth) / radius, y = (a1.elevation - centre.elevation) * kExtentShapeRatio / radius;
      CHECK(x * x + y * y <= 1.0001f);

      for (int c = 0; c < b; c++) {
        auto q = OscillatorVoice::cloudPoint(centre, radius, c, rings, &coord_a);
        closest = min(closest, hypotf(a1.azimuth - q.azimuth, (a1.elevation - q.elevation) * kExtentShapeRatio));
      }
    }
    CHECK(same == count);
    CHECK(different >= count - 1); // the centre of a single bucket aside, every point moves
    if (count > 1) CHECK(closest > 0.5f);
  }

  // Through the voice: one note keeps its layout, another note gets another.
  ChannelConfiguration config(44100, 3);
  OscillatorArray array;
  array.voices = 32;
  array.spread = 1.0f;
  SphericalPosition position;
  position.distance = 1.0f;
  position.extent = 1.0f;
  auto make = [&](int row) { return OscillatorVoice(config, position, 1.0f, WaveformType::SINE, 1.0f, 0.5f, SendLevels{}, NoteCoordinate(1, row, 0), array); };
  auto first = make(4), again = make(4), next = make(5);
  CHECK(first.bucketCount() == next.bucketCount());
  bool moved = false;
  for (size_t b = 0; b < first.bucketCount(); b++) {
    CHECK(first.bucketDirection(b).azimuth == again.bucketDirection(b).azimuth);
    if (fabsf(first.bucketDirection(b).azimuth - next.bucketDirection(b).azimuth) > 1e-3f) moved = true;
  }
  CHECK(moved);
}

// Drift is made of the detune: no detune, or a single member, means no drift.
TEST(oscillator_drift_needs_a_detuned_array) {
  CHECK(renderW(2.0f, 3, 0.0f, { 256 }, 22050) == renderW(0.0f, 3, 0.0f, { 256 }, 22050));
  CHECK(renderW(2.0f, 1, 8.0f, { 256 }, 22050) == renderW(0.0f, 1, 8.0f, { 256 }, 22050));
  CHECK(renderW(2.0f, 3, 8.0f, { 256 }, 22050) != renderW(0.0f, 3, 8.0f, { 256 }, 22050));
}

// Members blending one clock's points are independent of each other, and each
// matches a twin evaluated on a clock of its own.
TEST(pitch_drift_members_share_one_clock) {
  const double period = 3000.0;
  PitchDriftClock clock(period), twin_clock(period);
  PitchDriftMember first(11, 10.0f), second(22, 10.0f), twin_first(11, 10.0f), twin_second(22, 10.0f);

  double worst = 0.0;
  for (uint64_t t = 0; t < 40000; t += 37) {
    const PitchDriftPoint p = clock.at(t);
    worst = max(worst, fabs(first.deviation(p) - twin_first.deviation(twin_clock.at(t))));
    worst = max(worst, fabs(second.integral(p) - twin_second.integral(twin_clock.at(t))));
  }
  CHECK(worst < 1e-12);
  CHECK(fabs(first.deviation(clock.at(20000)) - second.deviation(clock.at(20000))) > 1e-6); // independent
}
