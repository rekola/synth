#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/state/SongState.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/WaveformType.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/Mixer.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace std;

namespace {

struct Cmd {
  int row;
  int track;
  const char * text;
};

struct Setup {
  Tuning tuning = Tuning::EDO12;
  float standard_pitch = 440.0f;
  int tracks = 1;
  int note_row = 0; // the note, A4, on track 0 (-1: none)
  int note_track = 0;
  vector<Cmd> commands;
};

struct Rig {
  Song song;
  ChannelConfiguration config{44100, 1};
  unique_ptr<Mixer> mixer;
  unique_ptr<SongState> state;
  int row = 0;

  explicit Rig(const Setup & s) {
    song.setTuning(s.tuning);
    song.setStandardPitch(s.standard_pitch);
    song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
    vector<int> ids;
    for (int t = 0; t < s.tracks; t++) ids.push_back(song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId());
    auto arrangement = song.getArrangement();
    if (s.note_row >= 0) arrangement.setNote(s.note_row, ids[static_cast<size_t>(s.note_track)], 0, Note(referenceNoteFor(s.tuning), 100));
    for (auto & c : s.commands) arrangement.setCommand(c.row, ids[static_cast<size_t>(c.track)], Command(c.text));
    mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    state = make_unique<SongState>(config);
    state->initialize(song);
    state->setIsPlaying(true);
    row = config.getSampleInterval(song.getTempo());
  }

  void play(int rows) {
    for (int r = 0; r < rows; r++) {
      state->renderBlock(row, song, *mixer);
      mixer->encode();
    }
  }

  // The left channel's frequency over the next row, from its zero crossings.
  double measure() {
    state->renderBlock(row, song, *mixer);
    auto out = mixer->encode();
    const float * left = out.getChannelData(0);
    vector<double> times;
    for (int i = 1; i < out.numberOfFrames(); i++)
      if (left[i - 1] < 0 && left[i] >= 0) times.push_back(i - 1 + static_cast<double>(-left[i - 1]) / static_cast<double>(left[i] - left[i - 1]));
    if (times.size() < 3) return 0.0;
    return 44100.0 * static_cast<double>(times.size() - 1) / (times.back() - times.front());
  }
};

const Tuning kTunings[] = {Tuning::EDO12, Tuning::EDO31};

} // namespace

TEST(global_pitch_slide_command_parses) {
  CHECK(Command("YU10").isGlobalPitchSlide());
  CHECK(Command("YD08").isGlobalPitchSlide());
  CHECK(!Command("YU10").isPitchSlide());
  CHECK(!Command("0U10").isGlobalPitchSlide());
  CHECK(Command("YU10").getPitchSlideCode() == 16);
  CHECK(Command("YD08").isPitchSlideDown());
}

TEST(standard_pitch_defaults_to_440_and_rejects_non_positive_values) {
  Song song;
  CHECK(song.getStandardPitch() == 440.0f);
  song.setStandardPitch(0.0f);
  song.setStandardPitch(-3.0f);
  song.setStandardPitch(NAN);
  CHECK(song.getStandardPitch() == 440.0f);
  song.setStandardPitch(1.5f); // absurd is allowed
  CHECK(song.getStandardPitch() == 1.5f);
}

TEST(standard_pitch_round_trips_and_an_old_file_loads_at_440) {
  InstrumentProvider provider;
  Song old_song;
  CHECK(old_song.open(string(TESTS_FIXTURES_DIR) + "/center_note.xml", provider));
  CHECK(old_song.getStandardPitch() == 440.0f);

  Song song;
  song.addTrack(make_unique<InstrumentTrack>(0));
  song.setStandardPitch(415.3f);
  auto path = string(TESTS_SCRATCH_DIR) + "/standard_pitch_round_trip.xml";
  song.save(path);
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK_NEAR(reloaded.getStandardPitch(), 415.3f, 1e-4f);
  filesystem::remove(path);
}

// A4 sounds at the standard pitch, and 880 is exactly an octave over 440.
TEST(standard_pitch_sets_a4_in_every_tuning) {
  for (auto tuning : kTunings) {
    Setup s;
    s.tuning = tuning;
    Rig base(s);
    base.play(1);
    double f440 = base.measure();
    CHECK_NEAR(f440, 440.0, 1.0);
    s.standard_pitch = 880.0f;
    Rig high(s);
    high.play(1);
    CHECK_NEAR(high.measure() / f440, 2.0, 0.003);
  }
}

// Changing it while a note sounds retunes the note.
TEST(standard_pitch_change_retunes_a_sounding_note) {
  Setup s;
  Rig rig(s);
  rig.play(1);
  {
    Song::Edit edit(rig.song, "standard pitch");
    rig.song.setStandardPitch(432.0f);
  }
  rig.play(1);
  CHECK_NEAR(rig.measure(), 432.0, 1.0);
}

// 0U10 then 0U00 x3 raises four steps; 0D00 then lowers one.
TEST(pitch_slide_00_repeats_the_last_amount_in_either_direction) {
  for (auto tuning : kTunings) {
    int edo = edoStepsFor(tuning);
    Setup s;
    s.tuning = tuning;
    s.commands = {{0, 0, "0U10"}, {1, 0, "0U00"}, {2, 0, "0U00"}, {3, 0, "0U00"}};
    Rig up(s);
    up.play(4);
    CHECK_NEAR(up.measure() / 440.0, exp2(4.0 / edo), 0.003);
    s.commands.push_back({4, 0, "0D00"});
    Rig down(s);
    down.play(5);
    CHECK_NEAR(down.measure() / 440.0, exp2(3.0 / edo), 0.003);
  }
}

// YUxx moves a held note exactly as 0Uxx does, row for row.
TEST(global_pitch_slide_matches_the_track_slide) {
  for (auto tuning : kTunings) {
    Setup s;
    s.tuning = tuning;
    s.commands = {{0, 0, "0U18"}, {1, 0, "0U00"}, {2, 0, "0D04"}};
    Rig track(s);
    s.commands = {{0, 0, "YU18"}, {1, 0, "YU00"}, {2, 0, "YD04"}};
    Rig global(s);
    for (int r = 0; r < 4; r++) CHECK_NEAR(global.measure() / track.measure(), 1.0, 0.002);
  }
}

// A note started on another track after the slide sounds at the offset.
TEST(global_pitch_reaches_a_later_note_on_another_track) {
  for (auto tuning : kTunings) {
    int edo = edoStepsFor(tuning);
    Setup s;
    s.tuning = tuning;
    s.tracks = 2;
    s.note_row = 2;
    s.note_track = 1;
    s.commands = {{0, 0, "YU10"}};
    Rig rig(s);
    rig.play(2);
    CHECK_NEAR(rig.measure() / 440.0, exp2(1.0 / edo), 0.003);
  }
}

TEST(global_pitch_memory_is_the_songs_and_apart_from_the_tracks) {
  Setup s;
  s.note_row = -1;
  s.tracks = 2;
  s.commands = {{0, 0, "0U20"}, {1, 1, "YU10"}, {2, 0, "YU00"}, {3, 1, "YD00"}, {4, 0, "0U00"}};
  Rig rig(s);
  rig.play(5);
  // +16 +16 -16: the track's 0U20 never became YU's amount.
  CHECK_NEAR(rig.state->getGlobalPitchUnits(), 16.0, 1e-9);
}

TEST(global_pitch_slides_of_several_tracks_on_a_row_add_up) {
  Setup s;
  s.note_row = -1;
  s.tracks = 2;
  s.commands = {{0, 0, "YU08"}, {0, 1, "YU08"}};
  Rig rig(s);
  rig.play(1);
  CHECK_NEAR(rig.state->getGlobalPitchUnits(), 16.0, 1e-9); // one step
}

// An octave of offset (16 * steps) over a standard pitch of 415 gives A4 at 830.
TEST(global_pitch_octave_over_415_is_830) {
  for (auto tuning : kTunings) {
    int edo = edoStepsFor(tuning);
    Setup s;
    s.tuning = tuning;
    s.standard_pitch = 415.0f;
    s.note_row = edo + 1;
    for (int r = 0; r < edo; r++) s.commands.push_back({r, 0, "YU10"});
    Rig rig(s);
    rig.play(edo + 1);
    CHECK_NEAR(rig.state->getGlobalPitchUnits(), 16.0 * edo, 1e-9);
    CHECK_NEAR(rig.measure(), 830.0, 1.5);
  }
}

TEST(global_pitch_after_a_seek_is_what_playing_there_gives) {
  Setup s;
  s.note_row = -1;
  s.tracks = 2;
  s.commands = {{0, 0, "YU10"}, {1, 1, "YU00"}, {3, 0, "YD05"}, {5, 1, "YU00"}, {6, 0, "YU22"}};
  for (int target = 0; target < 8; target++) {
    Rig played(s);
    played.play(target);
    Rig seeked(s);
    seeked.play(3);
    seeked.state->setPosition(target);
    seeked.play(0);
    seeked.state->renderBlock(1, seeked.song, *seeked.mixer); // the rescan runs at a block's start
    played.state->renderBlock(1, played.song, *played.mixer);
    CHECK_NEAR(seeked.state->getGlobalPitchUnits(), played.state->getGlobalPitchUnits(), 1e-6);
  }
  // A restart from the top is 0 again.
  Rig rig(s);
  rig.play(4);
  rig.state->setPosition(0);
  rig.state->renderBlock(1, rig.song, *rig.mixer); // one frame into row 0's own YU10
  CHECK(fabs(rig.state->getGlobalPitchUnits()) < 0.01);
}

TEST(global_pitch_survives_a_pattern_break) {
  Setup s;
  s.note_row = -1;
  s.commands = {{0, 0, "YU10"}, {1, 0, "ZB00"}};
  Rig rig(s);
  rig.play(3); // row 2 is the next bar's first row (16), the offset kept
  CHECK(rig.state->getAbsolutePosition() == 17);
  CHECK_NEAR(rig.state->getGlobalPitchUnits(), 16.0, 1e-9);
}

// Runs far past anything audible without a NaN or an infinity.
TEST(global_pitch_slide_to_extremes_stays_finite) {
  for (const char * cmd : {"YUFF", "YDFF"}) {
    Setup s;
    for (int r = 0; r < 600; r++) s.commands.push_back({r, 0, r == 0 ? cmd : (cmd[1] == 'U' ? "YU00" : "YD00")});
    s.note_row = 0;
    Rig rig(s);
    bool finite = true;
    for (int r = 0; r < 600; r++) {
      rig.state->renderBlock(rig.row, rig.song, *rig.mixer);
      auto out = rig.mixer->encode();
      for (int ch = 0; ch < 2; ch++) {
        const float * d = out.getChannelData(ch);
        for (int i = 0; i < out.numberOfFrames(); i++) finite = finite && std::isfinite(d[i]);
      }
    }
    CHECK(finite);
    CHECK(fabs(rig.state->getGlobalPitchUnits()) > 600.0 * 255.0 - 1.0);
  }
}
