#include "TestFramework.h"
#include "Sf2Fixture.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/Note.h"
#include "../src/model/SharedOvertones.h"
#include "../src/state/SongState.h"
#include "../src/state/MemoryParameterSource.h"
#include "../src/instruments/Additive.h"
#include "../src/instruments/FM.h"
#include "../src/instruments/Noise.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/OvertoneComb.h"
#include "../src/instruments/OvertoneProfile.h"
#include "../src/instruments/PadSynth.h"
#include "../src/instruments/SoundFont.h"
#include "../src/instruments/WaveformType.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/Mixer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

namespace {
int key(Tuning tuning, const char * name) { return Note::stringToKey(tuning, name); }
} // namespace

// ---- which overtones are shared ----

TEST(overtone_glide_command_parses_rate_and_instant) {
  Command half("0H08");
  CHECK(half.isOvertoneGlide());
  CHECK(!half.isPitchGlide());
  CHECK(half.getOvertoneGlideCode() == 8);
  CHECK(!half.isOvertoneGlideInstant());
  CHECK(Command("0HFF").isOvertoneGlideInstant());
  CHECK(Command("0H00").getOvertoneGlideCode() == 0);
  CHECK(!Command("0G10").isOvertoneGlide());
}

// Over a sounding C4 the target's ratio p/q, in lowest terms, raises every p-th overtone.
TEST(shared_overtones_come_from_the_gamut_ratios) {
  const Tuning t = Tuning::EDO53;
  auto scale = just_intonation::scaleCorrectionsFor(t, Scale::OTONAL, 0);
  const int c4 = key(t, "C4");
  auto step = [&](const char * target) { return shared_overtones::step(scale, 53, c4, key(t, target)); };
  CHECK(step("C5") == 2);
  CHECK(step("C6") == 4);
  CHECK(step("G4") == 3);
  CHECK(step("G5") == 3);
  CHECK(step("E4") == 5);
  CHECK(step("A♯↑4") == 7);
  // At the note's own pitch or any octave below it every overtone is shared:
  // the plain profile.
  CHECK(step("C4") == 1);
  CHECK(step("C3") == 1);
  CHECK(step("C2") == 1);
  // From another sounding note: C5 over G4 is 4/3, G4 over E4 6/5.
  CHECK(shared_overtones::step(scale, 53, key(t, "G4"), key(t, "C5")) == 4);
  CHECK(shared_overtones::step(scale, 53, key(t, "E4"), key(t, "G4")) == 6);
}

// The ratios are the key's, so a song in D finds D-A a fifth.
TEST(shared_overtones_follow_the_songs_key) {
  const Tuning t = Tuning::EDO12;
  auto scale = just_intonation::scaleCorrectionsFor(t, Scale::MAJOR, key(t, "D4"));
  CHECK(shared_overtones::step(scale, 12, key(t, "D4"), key(t, "A4")) == 3);
  CHECK(shared_overtones::step(scale, 12, key(t, "D4"), key(t, "F♯4")) == 5);
}

// With no gamut only whole octaves match exactly.
TEST(without_a_gamut_only_whole_octaves_share_overtones) {
  const Tuning t = Tuning::EDO53;
  auto scale = just_intonation::scaleCorrectionsFor(t, Scale::NONE, 0);
  CHECK(scale.empty());
  const int c4 = key(t, "C4");
  auto step = [&](const char * target) { return shared_overtones::step(scale, 53, c4, key(t, target)); };
  CHECK(step("C5") == 2);
  CHECK(step("C6") == 4);
  CHECK(step("G4") == 0);
  CHECK(step("E4") == 0);
  CHECK(step("C4") == 1);
  CHECK(step("C3") == 1);
}

// ---- the profile ----

TEST(a_raised_overtone_is_twelve_db_up) {
  CHECK(OvertoneProfile::kRaiseDb == 12.0);
  OvertoneProfile profile;
  CHECK(!profile.engaged() && !profile.raised());
  profile.setTarget(2);
  profile.jump();
  CHECK_NEAR(profile.gain(4), std::pow(10.0, 12.0 / 20.0), 1e-9);
  CHECK(profile.gain(3) == 1.0);
}

// The profile moves by the rate given for the frames given, and holds at rate 0.
TEST(overtone_profile_moves_at_its_rate_and_holds) {
  OvertoneProfile profile;
  profile.setTarget(3);
  profile.advance(0.5 / 100.0, 100);
  CHECK(profile.amount(3) == 0.0);
  CHECK_NEAR(profile.amount(3, true), 0.5, 1e-12);
  CHECK(profile.amount(2, true) == 0.0);
  profile.advance(0.0, 100); // a row without the command
  CHECK_NEAR(profile.amount(6), 0.5, 1e-12);
  CHECK_NEAR(profile.amount(6, true), 0.5, 1e-12);
  profile.advance(0.5 / 50.0, 100); // twice as far as is left: stops at the target
  CHECK_NEAR(profile.amount(9, true), 1.0, 1e-12);
}

// From one target to another the old overtones fall as the new ones rise;
// an overtone both raise stays up.
TEST(overtone_profile_moves_from_one_target_to_another) {
  OvertoneProfile profile;
  profile.setTarget(2);
  profile.jump();
  profile.setTarget(3);
  profile.advance(0.25 / 10.0, 10);
  CHECK_NEAR(profile.amount(2, true), 0.75, 1e-12);
  CHECK_NEAR(profile.amount(3, true), 0.25, 1e-12);
  CHECK_NEAR(profile.amount(6, true), 1.0, 1e-12);
  CHECK(profile.amount(1, true) == 0.0);
  profile.advance(1.0, 10);
  CHECK(profile.amount(2, true) == 0.0);
  CHECK(profile.amount(4, true) == 0.0);
  CHECK(profile.amount(3, true) == 1.0);
  CHECK(profile.amount(6, true) == 1.0);
  profile.advance(0.0, 10);
  CHECK(profile.layerCount() == 1);
}

// Retargeting quickly never lifts an overtone past the full amount.
TEST(overtone_profile_never_passes_the_full_amount) {
  OvertoneProfile profile;
  const int steps[] = {2, 3, 4, 6, 12, 2, 5};
  for (int step : steps) {
    profile.setTarget(step);
    profile.advance(0.3, 1);
    for (int n = 1; n <= 60; n++) CHECK(profile.amount(n, true) <= 1.0 + 1e-12);
  }
}

// A target under 2 is the plain profile.
TEST(overtone_profile_target_at_the_notes_pitch_is_plain) {
  OvertoneProfile profile;
  profile.setTarget(2);
  profile.jump();
  profile.setTarget(1);
  profile.jump();
  CHECK(!profile.raised());
  for (int n = 1; n <= 12; n++) CHECK(profile.gain(n) == 1.0);
}

// ---- measuring ----

namespace {
constexpr int kRate = 44100;

// Hann-windowed amplitude of `signal` at `hz` over [from, to).
double amplitudeAt(const float * signal, int from, int to, double hz) {
  double re = 0.0, im = 0.0;
  const int n = to - from;
  for (int i = 0; i < n; i++) {
    const double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));
    const double phase = 2.0 * M_PI * hz * i / kRate;
    re += w * signal[from + i] * cos(phase);
    im -= w * signal[from + i] * sin(phase);
  }
  return sqrt(re * re + im * im);
}

// Marks an overtone too quiet to measure (a NAN would not survive -ffast-math).
constexpr double kUnmeasured = -1000.0;

// Each overtone's change in dB against `plain`, over [from, to); kUnmeasured
// for one too quiet in `plain` to measure (60 dB under the loudest).
vector<double> raiseDb(const float * raised, const float * plain, int from, int to, double f0, int count) {
  vector<double> base(static_cast<size_t>(count) + 1), shaped(static_cast<size_t>(count) + 1);
  double loudest = 0.0;
  for (int n = 1; n <= count; n++) {
    base[static_cast<size_t>(n)] = amplitudeAt(plain, from, to, f0 * n);
    shaped[static_cast<size_t>(n)] = amplitudeAt(raised, from, to, f0 * n);
    loudest = max(loudest, base[static_cast<size_t>(n)]);
  }
  vector<double> out(static_cast<size_t>(count) + 1, kUnmeasured);
  for (int n = 1; n <= count; n++) {
    if (base[static_cast<size_t>(n)] < loudest * 1e-3) continue;
    out[static_cast<size_t>(n)] = 20.0 * log10(shaped[static_cast<size_t>(n)] / base[static_cast<size_t>(n)]);
  }
  return out;
}

// Every measurable overtone that is a multiple of `step` (none when 0) is up
// by `expected_db`, and every other one moved less than 1 dB.
bool raisedAsExpected(const vector<double> & db, int step, double expected_db = OvertoneProfile::kRaiseDb, const char * what = "") {
  bool ok = true;
  int measured = 0;
  for (size_t n = 1; n < db.size(); n++) {
    if (db[n] == kUnmeasured) continue;
    measured++;
    const bool shared = step > 0 && n % static_cast<size_t>(step) == 0;
    const bool good = shared ? fabs(db[n] - expected_db) < 0.5 : fabs(db[n]) < 1.0;
    if (!good) {
      fprintf(stderr, "  %s overtone %zu: %+.2f dB (expected %+.1f)\n", what, n, db[n], shared ? expected_db : 0.0);
      ok = false;
    }
  }
  if (measured < 6) {
    fprintf(stderr, "  %s: only %d overtones measurable\n", what, measured);
    ok = false;
  }
  return ok;
}

using InstrumentFactory = function<unique_ptr<Track>()>;
using Setup = function<void(Song &, int)>;

struct Rendered {
  AudioBuffer out;
  int row = 0;
};

// Four rows of one track: `first` on row 0, the rest from `setup`.
Rendered renderRows(const InstrumentFactory & make, Tuning tuning, Scale scale, int first, const Setup & setup, int rows = 4) {
  Song song(tuning);
  song.setScale(scale);
  song.addInstrument(make());
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  const int id = track.getInternalId();
  song.getArrangement().setNote(0, id, 0, Note(first, 100));
  if (setup) setup(song, id);

  ChannelConfiguration config(kRate, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);
  Rendered r;
  r.row = config.getSampleInterval(song.getTempo());
  state.renderBlock(rows * r.row - 1, song, *mixer);
  r.out = mixer->encode();
  return r;
}

// The change of each of the first 16 overtones of `first` in row `row` of
// the song made by `setup`, against the song with only the first note (or
// `plain_setup`'s).
vector<double> overtoneChange(const InstrumentFactory & make, Tuning tuning, Scale scale, int first, const Setup & setup, int row,
                              const Setup & plain_setup = nullptr, double f0 = 0.0) {
  auto shaped = renderRows(make, tuning, scale, first, setup);
  auto plain = renderRows(make, tuning, scale, first, plain_setup);
  if (f0 <= 0.0) f0 = getFrequencyFor(tuning, first);
  return raiseDb(shaped.out.getChannelData(0), plain.out.getChannelData(0), row * shaped.row + 400, (row + 1) * shaped.row - 10, f0, 16);
}

Setup targetOnRow(int row, int note, const char * command) {
  return [=](Song & song, int id) {
    auto arrangement = song.getArrangement();
    arrangement.setNote(row, id, 0, Note(note, 100));
    if (command) arrangement.setCommand(row, id, Command(command));
  };
}

InstrumentFactory saw() {
  return [] { return make_unique<Oscillator>(WaveformType::SAW); };
}

InstrumentFactory fm() {
  return [] {
    auto instrument = make_unique<FM>();
    MemoryParameterSource params;
    params.set("ratio", 1.0f);
    params.set("index", 12.0f); // scaled by velocity: enough to give a dozen measurable harmonics
    instrument->loadParameters(params);
    return instrument;
  };
}

InstrumentFactory additive() {
  return [] {
    auto instrument = make_unique<Additive>();
    MemoryParameterSource params;
    params.set("stretch", 0.0f); // partials exactly on the harmonics, where they are measured
    params.set("unisonVoices", 1);
    params.set("thump", 0.0f);
    instrument->loadParameters(params);
    return instrument;
  };
}

InstrumentFactory padSynth() {
  return [] {
    auto instrument = make_unique<PadSynth>();
    MemoryParameterSource params;
    // Harmonic partial positions and narrow bands: a wide band's tails lie
    // under the neighbouring overtones, where measuring one cannot tell them apart.
    params.set("preset", string("saw-piano"));
    instrument->loadParameters(params);
    return instrument;
  };
}

// A SoundFont whose one sample is 16 harmonics of C4 (12-EDO), its root key.
InstrumentFactory soundFont() {
  static shared_ptr<SoundFont> font;
  if (!font) {
    const double f0 = getFrequencyFor(Tuning::EDO12, 60);
    vector<int16_t> samples(200000);
    for (size_t i = 0; i < samples.size(); i++) {
      double v = 0.0;
      for (int h = 1; h <= 16; h++) v += sin(2.0 * M_PI * h * f0 * static_cast<double>(i) / 44100.0) / h;
      samples[i] = static_cast<int16_t>(2800.0 * v);
    }
    auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "overtone_glide_harmonics.sf2").string();
    sf2fixture::writeMinimalSf2(path, {{"Harmonics", 0, {}, {}}}, samples);
    font = make_shared<SoundFont>(path);
  }
  return [] { return font->createInstrument(0, "test.harmonics"); };
}

struct NamedInstrument {
  const char * name;
  InstrumentFactory make;
};

vector<NamedInstrument> pitchedInstruments() {
  return {{"oscillator", saw()}, {"fm", fm()}, {"additive", additive()}, {"padsynth", padSynth()}, {"soundfont", soundFont()}};
}
} // namespace

// ---- the comb on its own ----

// On a periodic signal the comb raises exactly the shared harmonics.
TEST(overtone_comb_raises_exactly_the_shared_harmonics) {
  for (int step : {2, 3, 5, 7}) {
    const double f0 = 196.0;
    const int frames = 16384;
    vector<float> signal(frames), plain(frames);
    for (int i = 0; i < frames; i++) {
      double v = 0.0;
      for (int h = 1; h <= 20; h++) v += sin(2.0 * M_PI * h * f0 * i / kRate + h) / h;
      signal[static_cast<size_t>(i)] = plain[static_cast<size_t>(i)] = static_cast<float>(v);
    }
    OvertoneProfile profile;
    profile.setTarget(step);
    profile.jump();
    profile.advance(0.0, frames);
    OvertoneComb comb;
    comb.process(signal.data(), frames, kRate, f0, f0, profile);
    auto db = raiseDb(signal.data(), plain.data(), 2000, frames, f0, 20);
    CHECK(raisedAsExpected(db, step, 12.0, "comb"));
  }
}

// ---- the command in a song ----

// Over a sounding C4 a C5 with the command raises the even overtones, with
// every instrument, and does not retrigger the note or move its pitch.
TEST(overtone_glide_to_the_octave_raises_the_even_overtones) {
  for (const auto & instrument : pitchedInstruments()) {
    const Tuning t = Tuning::EDO12;
    const int c4 = key(t, "C4");
    auto db = overtoneChange(instrument.make, t, Scale::MAJOR, c4, targetOnRow(1, key(t, "C5"), "0H10"), 2);
    CHECK(raisedAsExpected(db, 2, 12.0, instrument.name));
  }
}

TEST(overtone_glide_to_a_fifth_and_a_third_raises_their_overtones) {
  for (const auto & instrument : pitchedInstruments()) {
    const Tuning t = Tuning::EDO12;
    const int c4 = key(t, "C4");
    CHECK(raisedAsExpected(overtoneChange(instrument.make, t, Scale::MAJOR, c4, targetOnRow(1, key(t, "G4"), "0HFF"), 2), 3, 12.0, instrument.name));
    CHECK(raisedAsExpected(overtoneChange(instrument.make, t, Scale::MAJOR, c4, targetOnRow(1, key(t, "E4"), "0HFF"), 2), 5, 12.0, instrument.name));
  }
}

// The septimal seventh of the otonal scale in 53-EDO: 7/4 raises 7 and 14.
TEST(overtone_glide_to_a_septimal_seventh_raises_the_seventh_overtones) {
  const Tuning t = Tuning::EDO53;
  auto db = overtoneChange(saw(), t, Scale::OTONAL, key(t, "C4"), targetOnRow(1, key(t, "A♯↑4"), "0HFF"), 2);
  CHECK(raisedAsExpected(db, 7, 12.0, "7/4"));
}

// The note is not retriggered: the start of the target's row is the plain
// note's, sample for sample.
TEST(overtone_glide_does_not_retrigger_the_note) {
  const Tuning t = Tuning::EDO12;
  auto shaped = renderRows(saw(), t, Scale::MAJOR, key(t, "C4"), targetOnRow(1, key(t, "C5"), "0H08"));
  auto plain = renderRows(saw(), t, Scale::MAJOR, key(t, "C4"), nullptr);
  float worst = 0.0f;
  for (int i = 0; i < shaped.row + 20; i++) worst = max(worst, fabsf(shaped.out.getChannelData(0)[i] - plain.out.getChannelData(0)[i]));
  CHECK(worst < 1e-3f);
}

// Tuning corrections change neither which overtones are raised nor by how much.
TEST(overtone_glide_ignores_tuning_corrections) {
  const Tuning t = Tuning::EDO53;
  const int c4 = key(t, "C4");
  for (const auto & instrument : {NamedInstrument{"oscillator", saw()}, NamedInstrument{"additive", additive()}}) {
    auto corrected = [&](Song & song, int id) {
      auto arrangement = song.getArrangement();
      Note sounding(c4, 100), target(key(t, "E4"), 100);
      CHECK(sounding.setFx("+50")); // 5 cents sharp
      CHECK(target.setFx("-D0"));   // 13 cents flat
      arrangement.setNote(0, id, 0, sounding);
      arrangement.setNote(1, id, 0, target);
      arrangement.setCommand(1, id, Command("0HFF"));
    };
    auto plain = [&](Song & song, int id) {
      Note sounding(c4, 100);
      CHECK(sounding.setFx("+50"));
      song.getArrangement().setNote(0, id, 0, sounding);
    };
    const double f0 = getFrequencyFor(t, c4) * exp2(5.0 / 1200.0);
    CHECK(raisedAsExpected(overtoneChange(instrument.make, t, Scale::OTONAL, c4, corrected, 2, plain, f0), 5, 12.0, instrument.name));
    CHECK(raisedAsExpected(overtoneChange(instrument.make, t, Scale::OTONAL, c4, targetOnRow(1, key(t, "E4"), "0HFF"), 2), 5, 12.0, instrument.name));
  }
}

// With no gamut an octave up raises the even overtones and a fifth nothing.
TEST(overtone_glide_without_a_gamut_matches_only_octaves) {
  const Tuning t = Tuning::EDO53;
  const int c4 = key(t, "C4");
  CHECK(raisedAsExpected(overtoneChange(saw(), t, Scale::NONE, c4, targetOnRow(1, key(t, "C5"), "0HFF"), 2), 2, 12.0, "octave"));
  CHECK(raisedAsExpected(overtoneChange(saw(), t, Scale::NONE, c4, targetOnRow(1, key(t, "G4"), "0HFF"), 2), 0, 12.0, "fifth"));
}

// H08 does half the change in its row and stops there; H00 on the next row
// carries it to the end; HFF is all of it at once.
TEST(overtone_glide_is_timed_like_a_pitch_glide) {
  const Tuning t = Tuning::EDO12;
  const int c4 = key(t, "C4"), c5 = key(t, "C5");
  CHECK(raisedAsExpected(overtoneChange(saw(), t, Scale::MAJOR, c4, targetOnRow(1, c5, "0H08"), 2), 2, 6.0, "H08, one row"));
  CHECK(raisedAsExpected(overtoneChange(saw(), t, Scale::MAJOR, c4, targetOnRow(1, c5, "0H08"), 3), 2, 6.0, "H08, held"));
  auto carried = [&](Song & song, int id) {
    targetOnRow(1, c5, "0H08")(song, id);
    song.getArrangement().setCommand(2, id, Command("0H00"));
  };
  CHECK(raisedAsExpected(overtoneChange(saw(), t, Scale::MAJOR, c4, carried, 3), 2, 12.0, "H08 then H00"));
  CHECK(raisedAsExpected(overtoneChange(saw(), t, Scale::MAJOR, c4, targetOnRow(1, c5, "0HFF"), 1), 2, 12.0, "HFF"));
}

// As a note's fx, and H00 in a cell with no note, the same.
TEST(overtone_glide_works_as_a_notes_fx_and_carries_on_in_an_fx_only_cell) {
  const Tuning t = Tuning::EDO12;
  auto setup = [&](Song & song, int id) {
    auto arrangement = song.getArrangement();
    Note target(key(t, "C5"), 100), carry;
    CHECK(target.setFx("H08"));
    CHECK(carry.setFx("H00"));
    arrangement.setNote(1, id, 0, target);
    arrangement.setNote(2, id, 0, carry);
  };
  CHECK(raisedAsExpected(overtoneChange(saw(), t, Scale::MAJOR, key(t, "C4"), setup, 3), 2, 12.0, "fx"));
}

// H00 with no rate issued does nothing: the row's note plays as usual.
TEST(overtone_glide_h00_without_an_earlier_rate_plays_the_note) {
  const Tuning t = Tuning::EDO12;
  auto shaped = renderRows(saw(), t, Scale::MAJOR, key(t, "C4"), targetOnRow(1, key(t, "C5"), "0H00"));
  auto played = renderRows(saw(), t, Scale::MAJOR, key(t, "C4"), targetOnRow(1, key(t, "C5"), nullptr));
  float worst = 0.0f;
  for (int i = 0; i < shaped.out.numberOfFrames(); i++) worst = max(worst, fabsf(shaped.out.getChannelData(0)[i] - played.out.getChannelData(0)[i]));
  CHECK(worst < 1e-6f);
}

// A second target ends with only its own overtones raised.
TEST(overtone_glide_to_a_new_target_leaves_only_its_overtones_raised) {
  const Tuning t = Tuning::EDO12;
  for (const auto & instrument : pitchedInstruments()) {
    auto setup = [&](Song & song, int id) {
      targetOnRow(1, key(t, "C5"), "0H10")(song, id);
      targetOnRow(2, key(t, "G4"), "0H10")(song, id);
    };
    CHECK(raisedAsExpected(overtoneChange(instrument.make, t, Scale::MAJOR, key(t, "C4"), setup, 3), 3, 12.0, instrument.name));
  }
}

// A target at the note's own pitch returns it to the plain profile.
TEST(overtone_glide_to_the_notes_own_pitch_is_plain) {
  const Tuning t = Tuning::EDO12;
  const int c4 = key(t, "C4");
  auto setup = [&](Song & song, int id) {
    targetOnRow(1, key(t, "C5"), "0HFF")(song, id);
    targetOnRow(2, c4, "0HFF")(song, id);
  };
  CHECK(raisedAsExpected(overtoneChange(saw(), t, Scale::MAJOR, c4, setup, 3), 0, 12.0, "own pitch"));
}

// A pitch glide after the profile has changed keeps the same overtone numbers raised.
TEST(overtone_glide_survives_a_pitch_glide) {
  const Tuning t = Tuning::EDO12;
  const int c4 = key(t, "C4"), d4 = key(t, "D4");
  for (const auto & instrument : {NamedInstrument{"oscillator", saw()}, NamedInstrument{"padsynth", padSynth()}, NamedInstrument{"soundfont", soundFont()}}) {
    auto setup = [&](Song & song, int id) {
      targetOnRow(1, key(t, "C5"), "0HFF")(song, id);
      targetOnRow(2, d4, "0GFF")(song, id);
    };
    auto plain = targetOnRow(2, d4, "0GFF");
    CHECK(raisedAsExpected(overtoneChange(instrument.make, t, Scale::MAJOR, c4, setup, 3, plain, getFrequencyFor(t, d4)), 2, 12.0, instrument.name));
  }
}

// A note started normally afterwards has the plain profile.
TEST(overtone_glide_a_new_note_starts_plain) {
  const Tuning t = Tuning::EDO12;
  const int c4 = key(t, "C4");
  for (const auto & instrument : pitchedInstruments()) {
    auto setup = [&](Song & song, int id) {
      targetOnRow(1, key(t, "C5"), "0HFF")(song, id);
      targetOnRow(2, c4, nullptr)(song, id);
    };
    CHECK(raisedAsExpected(overtoneChange(instrument.make, t, Scale::MAJOR, c4, setup, 3, targetOnRow(2, c4, nullptr)), 0, 12.0, instrument.name));
  }
}

// With nothing sounding in the column the note plays as usual.
TEST(overtone_glide_with_nothing_sounding_plays_the_note) {
  const Tuning t = Tuning::EDO12;
  auto commanded = [&](Song & song, int id) { song.getArrangement().setCommand(0, id, Command("0H10")); };
  auto shaped = renderRows(saw(), t, Scale::MAJOR, key(t, "C4"), commanded);
  auto plain = renderRows(saw(), t, Scale::MAJOR, key(t, "C4"), nullptr);
  float worst = 0.0f, peak = 0.0f;
  for (int i = 0; i < shaped.out.numberOfFrames(); i++) {
    worst = max(worst, fabsf(shaped.out.getChannelData(0)[i] - plain.out.getChannelData(0)[i]));
    peak = max(peak, fabsf(plain.out.getChannelData(0)[i]));
  }
  CHECK(peak > 0.01f);
  CHECK(worst < 1e-6f);
}

// Noise has no overtones: the command leaves it as it was.
TEST(overtone_glide_does_nothing_to_noise) {
  const Tuning t = Tuning::EDO12;
  InstrumentFactory noise = [] { return make_unique<Noise>(); };
  auto shaped = renderRows(noise, t, Scale::MAJOR, key(t, "C4"), targetOnRow(1, key(t, "C5"), "0HFF"));
  auto plain = renderRows(noise, t, Scale::MAJOR, key(t, "C4"), nullptr);
  float worst = 0.0f;
  for (int i = 0; i < shaped.out.numberOfFrames(); i++) worst = max(worst, fabsf(shaped.out.getChannelData(0)[i] - plain.out.getChannelData(0)[i]));
  CHECK(worst < 1e-6f);
}
