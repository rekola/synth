#include "TestFramework.h"

#include "../src/instruments/Oscillator.h"
#include "../src/instruments/OscillatorArray.h"
#include "../src/instruments/OscillatorArrayVoice.h"
#include "../src/instruments/OscillatorVoice.h"
#include "../src/instruments/NoteMultiplier.h"
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

// One array voice with several copies must sound like the same copies as
// separate OscillatorVoices (floor reflection off, so no per-copy/summed
// difference).
TEST(oscillator_array_voice_matches_separate_voices) {
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  SendLevels sends;
  NoteCoordinate coord(1, 16, 0);

  struct Spec { float ratio, velocity_scale, azimuth; };
  const Spec specs[] = { { 1.0f, 1.0f, -30.0f }, { 1.003f, 1.0f, 30.0f }, { 1.5f, 0.5f, 0.0f }, { 2.0f, 0.25f, 90.0f } };
  const float frequency = 330.0f, velocity = 0.8f;

  SphericalPosition centre;
  centre.distance = 2.0f;

  OscillatorArrayVoice array_voice(config, centre, sends, coord);
  vector<unique_ptr<OscillatorVoice>> separate;
  int id = 0;
  for (auto & s : specs) {
    SphericalPosition position = centre;
    position.azimuth += s.azimuth;
    array_voice.addCopy({ WaveformType::SAW, 0.7f, 0.5f, s.ratio, s.velocity_scale, position, coord.withInstance(id) });
    auto voice = make_unique<OscillatorVoice>(config, position, s.ratio, WaveformType::SAW, 0.7f, 0.5f, sends, coord.withInstance(id));
    voice->playNote(frequency, velocity * s.velocity_scale, 60);
    separate.push_back(move(voice));
    id++;
  }
  array_voice.playNote(frequency, velocity, 60);
  CHECK(array_voice.copyCount() == 4);

  int bad = 0, total = 0;
  for (int block = 0; block < 10; block++) {
    const int frames = 512;
    auto a = array_voice.render(frames);
    AudioBuffer expected(config.numberOfChannels(), frames);
    expected.zero();
    for (auto & v : separate) expected.mixNamed(v->render(frames));

    CHECK(a.regularChannelCount() == config.numberOfChannels());
    for (int c = 0; c < a.regularChannelCount(); c++) {
      for (int k = 0; k < frames; k++, total++) {
	if (fabsf(a.getChannelData(c)[k] - expected.getChannelData(c)[k]) > 1e-3f) bad++;
      }
    }
  }
  CHECK(bad < total / 500); // only a saw's wrap sample may differ
}

namespace {

// Counts the voices a NoteMultiplier hands back, and which of them are the
// merged array.
int countVoices(const VoiceState & v) { return v.getAllocatedVoiceCount(); }

unique_ptr<NoteMultiplier> multiplier(int unisons, int octaves, int fifths, int fourths) {
  auto m = make_unique<NoteMultiplier>();
  MemoryParameterSource params;
  params.set("unisons", unisons);
  params.set("octaves", octaves);
  params.set("fifths", fifths);
  params.set("fourths", fourths);
  params.set("detune", 4.0f);
  params.set("spread", 1.0f);
  m->loadParameters(params);
  auto osc = make_unique<Oscillator>(WaveformType::SQUARE);
  MemoryParameterSource osc_params;
  osc_params.set("type", string("square"));
  osc->loadParameters(osc_params);
  m->addChild(std::move(osc));
  return m;
}

} // namespace

TEST(note_multiplier_renders_oscillator_copies_as_one_voice) {
  auto m = multiplier(5, 1, 1, 1);

  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  position.extent = 1.0f;

  auto group = m->playNote(config, position, Tuning::TET31, 1.0f, 1.0f, 60, SendLevels{}, NoteCoordinate(0, 0, 0), false);
  CHECK(group.get() != nullptr);
  // The group plus exactly one child, however many copies.
  CHECK(countVoices(*group) == 2);

  for (int block = 0; block < 4; block++) {
    auto out = group->render(256);
    CHECK(out.hasChannel(Channel::Main));
    bool any = false;
    for (int c = 0; c < out.regularChannelCount(); c++) {
      for (int k = 0; k < 256; k++) {
	float s = out.getChannelData(c)[k];
	CHECK(std::isfinite(s));
	any = any || s != 0.0f;
      }
    }
    CHECK(any);
  }
}
