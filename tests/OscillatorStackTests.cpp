#include "TestFramework.h"

#include "../src/instruments/Oscillator.h"
#include "../src/instruments/OscillatorArray.h"
#include "../src/instruments/OscillatorVoice.h"
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

TEST(oscillator_array_matches_the_scalar_waveforms) {
  const double rate = 440.0 / 44100.0;
  const int frames = 1003; // not a multiple of the vector width
  const WaveformType types[] = { WaveformType::SINE, WaveformType::SAW, WaveformType::TRIANGLE, WaveformType::SQUARE };

  for (auto type : types) {
    OscillatorArray array;
    OscillatorArray::Copy copy;
    copy.type = type;
    copy.level = 0.5f;
    copy.pulse_width = 0.3f;
    copy.ratio = 1.5;
    copy.phase = 0.37;
    array.add(copy);

    vector<float> out(OscillatorArray::paddedFrames(frames));
    int mismatches = 0;
    double max_err = 0.0;
    // Several blocks, so the phase carry between them is exercised too.
    for (int block = 0; block < 20; block++) {
      array.renderCopy(0, rate, frames, out.data());
      for (int k = 0; k < frames; k++) {
	double phase = 0.37 + 1.5 * rate * (block * frames + k);
	double expected = 0.5 * reference(type, phase, 0.3);
	double err = fabs(out[static_cast<size_t>(k)] - expected);
	// A discontinuity can land one sample either side of the exact edge.
	if (err > 1e-4) mismatches++; else max_err = max(max_err, err);
      }
      array.advance(rate, frames);
    }
    CHECK(mismatches < 20 * frames / 1000);
    CHECK(max_err < 1e-4);
  }
}

TEST(oscillator_array_sine_polynomial_is_accurate) {
  double max_err = 0.0;
  for (int i = -1000; i <= 2000; i++) {
    float turns = static_cast<float>(i) / 1000.0f;
    max_err = max(max_err, fabs(static_cast<double>(OscillatorArray::sineTurns(turns)) - sin(2.0 * kPi * static_cast<double>(turns))));
  }
  CHECK(max_err < 2e-6);
}

// A stacked voice must sound like the same members played as separate
// single-member voices (floor reflection off, so no per-member/summed
// difference).
TEST(oscillator_stack_matches_separate_voices) {
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  SendLevels sends;
  NoteCoordinate coord(1, 16, 0);

  OscillatorStack stack;
  stack.voices = 4;
  stack.ratio = 1.5f;
  stack.falloff = 0.5f;
  stack.detune_cents = 12.0f;
  stack.spread = 1.0f;

  SphericalPosition centre;
  centre.distance = 2.0f;
  centre.extent = 3.0f;

  const float frequency = 330.0f, velocity = 0.8f, note_detune = 1.01f;
  OscillatorVoice stacked(config, centre, note_detune, WaveformType::SAW, 0.7f, 0.5f, sends, coord, stack);
  CHECK(stacked.memberCount() == 4);
  stacked.playNote(frequency, velocity, 60);

  const float half_width = atan2f(stack.spread * centre.extent, centre.distance) * 180.0f / static_cast<float>(kPi);
  vector<unique_ptr<OscillatorVoice>> separate;
  for (int k = 0; k < 4; k++) {
    float place = 2.0f * static_cast<float>(k) / 3.0f - 1.0f;
    SphericalPosition position = centre;
    position.azimuth += place * half_width;
    position.elevation += place * half_width / kExtentShapeRatio;
    float ratio = note_detune * powf(stack.ratio, static_cast<float>(k)) * powf(2.0f, place * stack.detune_cents / 2400.0f);
    auto voice = make_unique<OscillatorVoice>(config, position, ratio, WaveformType::SAW, 0.7f * powf(stack.falloff, static_cast<float>(k)), 0.5f, sends, coord.withInstance(k));
    voice->playNote(frequency, velocity, 60);
    separate.push_back(move(voice));
  }

  int bad = 0, total = 0;
  double energy = 0.0;
  for (int block = 0; block < 10; block++) {
    const int frames = 512;
    auto a = stacked.render(frames);
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

// The default stack is one member, rendered exactly like a plain voice.
TEST(oscillator_default_stack_is_a_single_member) {
  Oscillator osc(WaveformType::SQUARE);
  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;

  auto voice = osc.playNote(config, position, Tuning::TET31, 1.0f, 1.0f, 60, SendLevels{}, NoteCoordinate(0, 0, 0), false);
  CHECK(voice.get() != nullptr);
  CHECK(voice->getAllocatedVoiceCount() == 1);
}

TEST(oscillator_voices_attribute_builds_one_stacked_voice) {
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

  auto voice = osc.playNote(config, position, Tuning::TET31, 1.0f, 1.0f, 60, SendLevels{}, NoteCoordinate(0, 0, 0), false);
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
