#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/instruments/PadSynth.h"
#include "../src/instruments/PadSynthTable.h"
#include "../src/instruments/PadSynthPresets.h"
#include "../src/instruments/PadSynthVoice.h"
#include "../src/dsp/RealFFT.h"
#include "../src/instruments/InstrumentLibrary.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/state/MemoryParameterSource.h"
#include "../src/ambisonic/SphericalPosition.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

namespace {

struct Loaded {
  bool ok;
  Song song;
};

// Same shape as RenderTests.cpp's own loadFixture() (no SoundFont needed -
// this fixture only uses the built-in padsynth oscillator).
Loaded loadFixture(const char * name) {
  InstrumentProvider provider;
  Song song;
  bool ok = song.open(std::string(TESTS_FIXTURES_DIR) + "/" + name, provider);
  return { ok, std::move(song) };
}

bool hasNonFiniteSample(const OfflineRenderResult & result) {
  for (auto v : result.interleaved) {
    if (!std::isfinite(v)) return true;
  }
  return false;
}

float peakAbs(const OfflineRenderResult & result) {
  float peak = 0.0f;
  for (auto v : result.interleaved) peak = std::max(peak, std::fabs(v));
  return peak;
}

} // namespace

TEST(padsynth_note_renders_nonsilent_and_finite) {
  auto loaded = loadFixture("padsynth_note.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(!hasNonFiniteSample(result));
  CHECK(peakAbs(result) > 1e-4f);
}

TEST(padsynth_element_round_trips_through_save_and_load) {
  // <padsynth> attributes (preset/level) must survive a
  // save/reload the same way any other instrument's own attributes do -
  // loadFixture() above
  // only exercises the load half.
  auto loaded = loadFixture("padsynth_note.xml");
  CHECK(loaded.ok);

  auto tmp_path = std::string(TESTS_SCRATCH_DIR) + "/padsynth_roundtrip_scratch.xml";
  loaded.song.save(tmp_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(tmp_path, provider));

  ChannelConfiguration config(44100, 1);
  auto original_render = renderSongOffline(loaded.song, config);
  auto reloaded_render = renderSongOffline(reloaded, config);

  CHECK(!hasNonFiniteSample(reloaded_render));
  CHECK(original_render.interleaved.size() == reloaded_render.interleaved.size());
  bool all_equal = true;
  for (size_t i = 0; i < original_render.interleaved.size(); i++) {
    if (original_render.interleaved[i] != reloaded_render.interleaved[i]) { all_equal = false; break; }
  }
  CHECK(all_equal);

  std::remove(tmp_path.c_str());
}

TEST(padsynth_envelope_remap_attributes_round_trip_and_affect_render) {
  // The remap is genuinely wired into playback (not just parsed): a
  // fixture with envelopeAnchor/envelopeTracking/envelopePostprocess* set
  // should render differently from the same preset with them left at
  // their (off) default, and those attributes must survive a save/reload
  // the same way every other <padsynth> attribute does.
  auto remapped = loadFixture("padsynth_remap_note.xml");
  CHECK(remapped.ok);

  auto tmp_path = std::string(TESTS_SCRATCH_DIR) + "/padsynth_remap_roundtrip_scratch.xml";
  remapped.song.save(tmp_path);
  CHECK(std::ifstream(tmp_path).good());
  {
    std::ifstream in(tmp_path);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(contents.find("envelopeAnchor") != std::string::npos);
    CHECK(contents.find("envelopePostprocess=\"residue\"") != std::string::npos);
  }

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(tmp_path, provider));

  ChannelConfiguration config(44100, 1);
  auto remapped_render = renderSongOffline(remapped.song, config);
  auto reloaded_render = renderSongOffline(reloaded, config);
  CHECK(!hasNonFiniteSample(remapped_render));
  CHECK(remapped_render.interleaved.size() == reloaded_render.interleaved.size());
  bool all_equal = true;
  for (size_t i = 0; i < remapped_render.interleaved.size(); i++) {
    if (remapped_render.interleaved[i] != reloaded_render.interleaved[i]) { all_equal = false; break; }
  }
  CHECK(all_equal); // round trip is bit-identical

  std::remove(tmp_path.c_str());
}

TEST(extra_oscillator_presets_render_nonsilent_and_finite) {
  // choir-pad4/long-spacechoir2/bells-3/dual-strings/synth-piano-3-b aren't
  // wired into any InstrumentLibrary.cpp registration or the demo song -
  // this is their own coverage.
  auto loaded = loadFixture("padsynth_extra_presets.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(!hasNonFiniteSample(result));
  CHECK(peakAbs(result) > 1e-4f);
}

namespace {

// Rising zero crossings of the W channel over a few blocks of one note.
int risingCrossings(float detune_cents) {
  PadSynth pad;
  MemoryParameterSource params;
  params.set("preset", string("soft-pad")); // a single partial: a clean sine
  params.set("detune", detune_cents);
  pad.loadParameters(params);

  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  auto voice = pad.playNote(config, position, SpatialMode::AUTO, Tuning::EDO31, 1.0f, 1.0f, 160, SendLevels{}, NoteCoordinate(0, 0, 0)); // ~294 Hz

  int crossings = 0;
  float prev = 0.0f;
  for (int block = 0; block < 64; block++) {
    auto out = voice->render(1024);
    for (int k = 0; k < 1024; k++) {
      float v = out.getChannelData(0)[k];
      if (prev <= 0.0f && v > 0.0f) crossings++;
      prev = v;
    }
  }
  return crossings;
}

} // namespace

TEST(padsynth_detune_in_cents_shifts_pitch_and_round_trips) {
  int base = risingCrossings(0.0f);
  int octave = risingCrossings(1200.0f);
  CHECK(base > 20);
  // 1200 cents is an octave up: twice the cycles. The band has some width,
  // so a stretch of the table where it dips adds or drops a few crossings.
  CHECK(octave >= 1.85 * base && octave <= 2.15 * base);

  PadSynth pad;
  MemoryParameterSource params;
  params.set("detune", 700.0f);
  pad.loadParameters(params);
  MemoryParameterSource stored;
  pad.storeParameters(stored);
  CHECK_NEAR(stored.get<float>("detune", 0.0f), 700.0f, 1e-6f);

  PadSynth plain;
  MemoryParameterSource plain_stored;
  plain.storeParameters(plain_stored);
  CHECK(!plain_stored.has("detune")); // the default isn't written
}

namespace {

// A group of padsynth copies playing one note, summed.
AudioBuffer renderEnsemble(const vector<float> & cents, int frames, int row = 4) {
  vector<unique_ptr<PadSynth>> pads;
  vector<unique_ptr<VoiceState>> voices;
  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  AudioBuffer sum(config.numberOfChannels(), frames);
  sum.zero();
  for (float c : cents) {
    auto pad = make_unique<PadSynth>();
    MemoryParameterSource params;
    params.set("preset", string("strings"));
    params.set("detune", c);
    pad->loadParameters(params);
    voices.push_back(pad->playNote(config, position, SpatialMode::AUTO, Tuning::EDO31, 1.0f, 1.0f, 160, SendLevels{}, NoteCoordinate(1, row, 0)));
    pads.push_back(move(pad));
  }
  for (auto & v : voices) sum.mixNamed(v->render(frames));
  return sum;
}

double rmsOf(const AudioBuffer & b) {
  double e = 0.0;
  int n = b.numberOfFrames();
  for (int i = 0; i < n; i++) e += static_cast<double>(b.getChannelData(0)[i]) * static_cast<double>(b.getChannelData(0)[i]);
  return sqrt(e / n);
}

} // namespace

// Detuned copies of one note must not start phase-locked (they start at
// unrelated places in the table): three coherent copies would sum to three
// times one copy's level, independent ones to about the square root of three.
TEST(padsynth_detuned_ensemble_copies_start_at_different_phases) {
  const int frames = 1024; // well inside one beat period, so the copies stay near their start phases
  // Summed over many notes, the power of three independent copies is three
  // times one copy's; locked copies would give nine times.
  double single = 0.0, three = 0.0;
  for (int row = 0; row < 32; row++) {
    double s1 = rmsOf(renderEnsemble({ 0.0f }, frames, row));
    double s3 = rmsOf(renderEnsemble({ -14.0f, 0.0f, 14.0f }, frames, row));
    single += s1 * s1;
    three += s3 * s3;
  }
  CHECK(single > 1e-8);
  CHECK(three < 5.0 * single);
  CHECK(three > 1.5 * single);
}

TEST(library_ensemble_pads_are_three_detuned_padsynth_copies_and_render) {
  InstrumentProvider provider;
  registerLibraryInstruments(provider);
  for (const char * path : { "pad.poly", "pad.choir", "pad.bowed", "string.synth.slow", "brass.synth" }) {
    auto instrument = provider.resolvePath(path);
    CHECK(instrument != nullptr);
    if (!instrument) continue;

    ChannelConfiguration config(44100, 1);
    SphericalPosition position;
    position.distance = 1.0f;
    auto voice = instrument->playNote(config, position, SpatialMode::AUTO, Tuning::EDO31, 1.0f, 1.0f, 160, SendLevels{}, NoteCoordinate(0, 0, 0));
    CHECK(voice.get() != nullptr);
    // The envelope's group holds one voice per copy.
    CHECK(voice->getAllocatedVoiceCount() >= 4);
    auto out = voice->render(2048);
    bool any = false;
    for (int k = 0; k < 2048; k++) {
      float v = out.getChannelData(0)[k];
      CHECK(std::isfinite(v));
      any = any || v != 0.0f;
    }
    CHECK(any);
  }
}

namespace {

constexpr int kTableSampleRate = 44100;

// A table with the given harmonics at amplitude 1 and no spectrum shaping
// beyond the options set by `tweak`, in a single region near `region_hz`.
PadSynthParams harmonicParams(const std::vector<int> & harmonics, float region_hz) {
  PadSynthParams params;
  params.oscillator.base_function = OscillatorShaping::BaseFunction::None;
  for (int h : harmonics) params.oscillator.harmonics.push_back({ h, 1.0f });
  params.bandwidth_cents = 20.0f;
  params.base_frequency_hz = region_hz;
  params.octaves = 0; // a single region, at region_hz
  params.samples_per_octave = 1;
  params.table_length = 1 << 16;
  return params;
}

// Amplitude of harmonic h in a table whose fundamental is f0: the root of
// the spectrum's power within a quarter fundamental of h * f0.
double harmonicAmplitude(const std::vector<float> & table, float f0, int h) {
  RealFFT<float> fft(table.size());
  auto & spectrum = fft.forward(table);
  double bin_hz = static_cast<double>(kTableSampleRate) / static_cast<double>(table.size());
  double lo = (h - 0.25) * f0 / bin_hz, hi = (h + 0.25) * f0 / bin_hz;
  double power = 0.0;
  for (size_t b = static_cast<size_t>(lo); b <= static_cast<size_t>(hi) && b < spectrum.size(); b++) power += std::norm(spectrum[b]);
  return std::sqrt(power);
}

} // namespace

TEST(padsynth_envelope_remap_reads_harmonic_one_from_the_first_slot) {
  std::vector<int> all;
  for (int h = 1; h <= 255; h++) all.push_back(h);
  PadSynthParams params = harmonicParams(all, 72.0f);
  params.envelope_anchor_hz = 289.4f;
  params.envelope_tracking = 1.0f;
  PadSynthTable table(kTableSampleRate, params);

  float f0 = table.tableBaseFrequency(72.0f);
  float r = f0 / 289.4f; // gather: harmonic h reads the flat prototype at h * r
  CHECK(r > 0.2f && r < 0.3f);
  const auto & wave = table.getTable(72.0f);
  double ref = harmonicAmplitude(wave, f0, 6); // 6 * r > 1: full level
  CHECK_NEAR(harmonicAmplitude(wave, f0, 1) / ref, r, 0.03);
  CHECK_NEAR(harmonicAmplitude(wave, f0, 2) / ref, 2.0f * r, 0.05);
  CHECK_NEAR(harmonicAmplitude(wave, f0, 3) / ref, 3.0f * r, 0.07);
}

TEST(padsynth_residue_postprocess_keeps_the_harmonics_it_names) {
  // n = 2, r = 0 with full strength keeps the even harmonics only.
  PadSynthParams params = harmonicParams({ 1, 2, 3, 4 }, 100.0f);
  params.postprocess_kind = SpectralPostprocessKind::ResidueClassWeighting;
  params.postprocess_n = 2;
  params.postprocess_r = 0;
  params.postprocess_amount = 1.0f;
  PadSynthTable table(kTableSampleRate, params);
  float f0 = table.tableBaseFrequency(100.0f);
  const auto & wave = table.getTable(100.0f);
  double even = harmonicAmplitude(wave, f0, 2);
  CHECK(even > 0.0);
  CHECK(harmonicAmplitude(wave, f0, 4) > 0.5 * even);
  CHECK(harmonicAmplitude(wave, f0, 1) < 0.05 * even);
  CHECK(harmonicAmplitude(wave, f0, 3) < 0.05 * even);
}

TEST(padsynth_stretch_postprocess_moves_harmonic_h_to_n_times_h) {
  PadSynthParams params = harmonicParams({ 3 }, 100.0f);
  params.postprocess_kind = SpectralPostprocessKind::StretchMix;
  params.postprocess_n = 2;
  params.postprocess_amount = 1.0f;
  PadSynthTable table(kTableSampleRate, params);
  float f0 = table.tableBaseFrequency(100.0f);
  const auto & wave = table.getTable(100.0f);
  double moved = harmonicAmplitude(wave, f0, 6);
  CHECK(moved > 0.0);
  CHECK(harmonicAmplitude(wave, f0, 3) < 0.05 * moved);
  CHECK(harmonicAmplitude(wave, f0, 5) < 0.05 * moved);
}

namespace {

vector<float> firstBlock(const NoteCoordinate & coord, int frames) {
  PadSynth pad;
  MemoryParameterSource params;
  params.set("preset", string("strings"));
  pad.loadParameters(params);
  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  auto voice = pad.playNote(config, position, SpatialMode::AUTO, Tuning::EDO31, 1.0f, 1.0f, 160, SendLevels{}, coord);
  auto out = voice->render(frames);
  return vector<float>(out.getChannelData(0), out.getChannelData(0) + frames);
}

} // namespace

TEST(padsynth_notes_start_anywhere_in_the_table) {
  // Chord notes (columns), repeated notes (rows) and detuned copies
  // (instances) are spread over the whole table, not bunched near its
  // start.
  vector<float> starts;
  for (int row = 0; row < 8; row++)
    for (int column = 0; column < 4; column++)
      for (int instance = 0; instance < 2; instance++)
        starts.push_back(padSynthStartFraction(NoteCoordinate(1, row * 4, column).withInstance(instance)));
  float lo = 1.0f, hi = 0.0f;
  int in_each_quarter[4] = { 0, 0, 0, 0 };
  for (float f : starts) {
    CHECK(f >= 0.0f && f < 1.0f);
    lo = std::min(lo, f);
    hi = std::max(hi, f);
    in_each_quarter[static_cast<int>(f * 4.0f)]++;
  }
  CHECK(hi - lo > 0.8f);
  for (int q : in_each_quarter) CHECK(q >= 8);

  // The same coordinate always starts in the same place.
  CHECK(padSynthStartFraction(NoteCoordinate(1, 4, 1)) == padSynthStartFraction(NoteCoordinate(1, 4, 1)));
  CHECK(firstBlock(NoteCoordinate(0, 4, 1), 1024) == firstBlock(NoteCoordinate(0, 4, 1), 1024));
}

TEST(padsynth_lowest_region_carries_partials_above_harmonic_255) {
  // A power ramp falls off as 1/h, so it still has content at harmonic 300
  // (11.7 kHz at 39 Hz) that a 255-harmonic spectrum would drop.
  PadSynthParams params = harmonicParams({}, 39.0f);
  params.oscillator.base_function = OscillatorShaping::BaseFunction::PowerRamp;
  params.oscillator.base_shape_param = 0.3766f;
  params.oscillator.harmonics = { { 1, 1.0f } };
  PadSynthTable table(kTableSampleRate, params);
  float f0 = table.tableBaseFrequency(39.0f);
  const auto & wave = table.getTable(39.0f);
  CHECK(harmonicAmplitude(wave, f0, 300) > 0.0);
  CHECK(harmonicAmplitude(wave, f0, 300) < harmonicAmplitude(wave, f0, 1));
}

TEST(padsynth_regions_span_octaves_plus_one_centred_on_the_base) {
  PadSynthParams params = harmonicParams({ 1 }, 261.6f);
  params.octaves = 6;
  params.samples_per_octave = 2;
  PadSynthTable table(kTableSampleRate, params);

  // 7 octaves at 2 per octave: 14 regions, half an octave apart, from
  // 27.5 Hz to 2489 Hz around the base.
  CHECK_NEAR(table.tableBaseFrequency(1.0f), 27.5f, 0.4f); // within half a bin
  CHECK_NEAR(table.tableBaseFrequency(100000.0f), 2488.9f, 0.4f);
  vector<float> regions;
  for (float f = 20.0f; f < 4000.0f; f *= 1.01f) {
    float r = table.tableBaseFrequency(f);
    if (regions.empty() || std::fabs(r - regions.back()) > 0.01f) regions.push_back(r);
  }
  CHECK(regions.size() == 14);
  for (size_t i = 1; i < regions.size(); i++) CHECK_NEAR(regions[i] / regions[i - 1], std::sqrt(2.0f), 0.02f);
  // A note picks the region nearest in pitch.
  CHECK_NEAR(table.tableBaseFrequency(261.6f * 1.1f), 261.6f * std::exp2(0.25f), 0.5f);
}

TEST(padsynth_region_frequencies_are_whole_numbers_of_bins) {
  PadSynthParams params = getPadSynthPreset("saw-piano");
  PadSynthTable table(kTableSampleRate, params);
  double bin_hz = static_cast<double>(kTableSampleRate) / static_cast<double>(params.table_length);
  for (float f = 20.0f; f < 5000.0f; f *= 1.05f) {
    double bins = static_cast<double>(table.tableBaseFrequency(f)) / bin_hz;
    CHECK_NEAR(bins, std::round(bins), 1e-3);
  }
}

TEST(padsynth_narrow_lowest_partial_is_one_steady_bin) {
  // saw-piano's lowest region has a 1.3 cent band, about 0.2 bins wide: it
  // used to be split over two bins that beat against each other.
  PadSynthParams params = getPadSynthPreset("saw-piano");
  PadSynthTable table(kTableSampleRate, params);
  float f0 = table.tableBaseFrequency(1.0f);
  const auto & wave = table.getTable(1.0f);
  RealFFT<float> fft(wave.size());
  auto & spectrum = fft.forward(wave);
  double bin_hz = static_cast<double>(kTableSampleRate) / static_cast<double>(wave.size());
  size_t centre = static_cast<size_t>(std::lround(static_cast<double>(f0) / bin_hz));
  double window = 0.0, strongest = 0.0;
  for (size_t b = centre - 3; b <= centre + 3; b++) {
    double p = std::norm(spectrum[b]);
    window += p;
    strongest = std::max(strongest, p);
  }
  CHECK(window > 0.0);
  CHECK(strongest > 0.999 * window); // one bin: no beating
}

TEST(padsynth_voice_keeps_its_region_when_the_pitch_crosses_a_boundary) {
  PadSynthParams params = harmonicParams({ 1 }, 100.0f);
  params.octaves = 1;
  params.samples_per_octave = 2; // regions a half octave apart
  auto table = std::make_shared<PadSynthTable>(kTableSampleRate, params);

  // The boundary between two neighbouring regions.
  float below = 100.0f;
  float base = table->tableBaseFrequency(below);
  float boundary = below;
  while (table->tableBaseFrequency(boundary) == base) boundary *= 1.0001f;

  ChannelConfiguration config(kTableSampleRate, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  PadSynthVoice voice(config, position, 1.0f, table, 1.0f, SendLevels{}, NoteCoordinate(0, 0, 0));
  voice.playNote(boundary * 0.995f, 1.0f, 60);

  vector<float> out;
  for (int block = 0; block < 2; block++) {
    if (block == 1) voice.playNote(boundary * 1.005f, 1.0f, 60); // bent over the boundary
    auto b = voice.render(512);
    out.insert(out.end(), b.getChannelData(0), b.getChannelData(0) + 512);
  }
  float biggest_step = 0.0f, join_step = std::fabs(out[512] - out[511]);
  for (size_t i = 1; i < out.size(); i++) {
    if (i != 512) biggest_step = std::max(biggest_step, std::fabs(out[i] - out[i - 1]));
  }
  CHECK(biggest_step > 0.0f);
  CHECK(join_step < 2.0f * biggest_step); // no jump to a different table
}
