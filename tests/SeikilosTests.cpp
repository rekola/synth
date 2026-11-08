#include "TestFramework.h"

#include "../src/instruments/InstrumentProvider.h"
#include "../src/model/Gamut.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/Scale.h"
#include "../src/model/Song.h"

#include <cmath>
#include <fstream>
#include <map>
#include <string>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif
#ifndef TESTS_SONGS_DIR
#define TESTS_SONGS_DIR "."
#endif

using namespace ratio_literals;

TEST(kitmum_is_the_mixolydian_scale_c_d_e_f_g_a_b_flat_of_the_pythagorean_gamut) {
  std::vector<Ratio> ratios;
  for (auto i : scaleGamutIndices(Scale::KITMUM)) ratios.push_back(gamut::kPythagorean[i].ratio);
  CHECK((ratios == std::vector<Ratio>{1 / 1_r, 9 / 8_r, 81 / 64_r, 4 / 3_r, 3 / 2_r, 27 / 16_r, 16 / 9_r}));
  CHECK(scaleFromString(to_string(Scale::KITMUM)) == Scale::KITMUM);
  // Every gamut note is 3-limit and sits on its 53-EDO spelling's step.
  for (auto & note : gamut::kPythagorean) {
    CHECK(Note::stringToKey(Tuning::EDO53, std::string(note.spelling) + "4") - Note::stringToKey(Tuning::EDO53, "C-4") ==
          ((note.ratio.steps(53) % 53) + 53) % 53);
  }
}

TEST(the_seven_babylonian_modes_are_rotations_of_one_pattern) {
  const Scale modes[] = {Scale::NIS_TUHRIM, Scale::ISARTUM, Scale::EMBUBUM, Scale::NID_QABLIM, Scale::QABLITUM, Scale::KITMUM, Scale::PITUM};
  // Ionian-order degrees of the chain of fifths: the pattern 2 2 1 2 2 2 1 rotated.
  const int whole_semitone_pattern[7] = {2, 2, 1, 2, 2, 2, 1};
  // Church-mode rotation of each (Locrian = rotation 6, Ionian 0, Dorian 1, Phrygian 2, Lydian 3, Mixolydian 4, Aeolian 5).
  const int rotation[7] = {6, 0, 1, 2, 3, 4, 5};
  for (int m = 0; m < 7; m++) {
    auto scale = modes[m];
    CHECK(scaleGamut(scale).data() == gamut::kPythagorean.data());
    CHECK(scaleSupportsTuning(scale, Tuning::EDO53));
    CHECK(!scaleSupportsTuning(scale, Tuning::EDO31));
    CHECK(scaleFromString(to_string(scale)) == scale);
    auto indices = scaleGamutIndices(scale);
    CHECK(indices.size() == 7);
    if (indices.size() != 7) continue;
    // Interval pattern in 53-EDO steps (tone 9, limma 4) matches the rotation.
    for (int d = 0; d < 7; d++) {
      auto here = gamut::kPythagorean[indices[static_cast<size_t>(d)]].ratio.steps(53);
      auto next = d == 6 ? 53 : gamut::kPythagorean[indices[static_cast<size_t>(d + 1)]].ratio.steps(53);
      CHECK(next - here == (whole_semitone_pattern[(rotation[m] + d) % 7] == 2 ? 9 : 4));
    }
  }
}

TEST(kitmum_is_the_mixolydian_rotation_and_isartum_the_default_ionian) {
  std::vector<Ratio> ratios;
  for (auto i : scaleGamutIndices(Scale::KITMUM)) ratios.push_back(gamut::kPythagorean[i].ratio);
  CHECK((ratios == std::vector<Ratio>{1 / 1_r, 9 / 8_r, 81 / 64_r, 4 / 3_r, 3 / 2_r, 27 / 16_r, 16 / 9_r}));
  ratios.clear();
  for (auto i : scaleGamutIndices(Scale::ISARTUM)) ratios.push_back(gamut::kPythagorean[i].ratio);
  CHECK((ratios == std::vector<Ratio>{1 / 1_r, 9 / 8_r, 81 / 64_r, 4 / 3_r, 3 / 2_r, 27 / 16_r, 243 / 128_r}));
}

TEST(seikilos_plays_only_scale_notes_each_tuned_to_its_3_limit_ratio_in_144_rows) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_SONGS_DIR) + "/seikilos.xml", provider));
  CHECK(song.getTuning() == Tuning::EDO53);
  CHECK(song.getScale() == Scale::KITMUM);
  CHECK(song.getArrangementLength() == 144); // a 24-row intro, 96 rows of song, a 24-row outro

  // The tempo broadens over the outro, as tempo changes in the arrangement.
  CHECK((song.getTempoChanges() == std::map<int, int>{{120, 92}, {124, 88}, {128, 84}, {132, 80}}));

  const int key = song.getKey();
  CHECK(key == Note::stringToKey(Tuning::EDO53, "C-4"));
  auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::KITMUM, key);

  std::map<int, Ratio> ratio_of_step; // the scale's degrees
  for (auto i : scaleGamutIndices(Scale::KITMUM)) ratio_of_step.emplace(((gamut::kPythagorean[i].ratio.steps(53) % 53) + 53) % 53, gamut::kPythagorean[i].ratio);

  int notes = 0;
  auto arrangement = song.getArrangement();
  for (auto & [track_id, pattern] : arrangement.getPatternsByTrack()) {
    auto * track = song.getMasterTrack().getChildByInternalId(track_id);
    if (!track || dynamic_cast<const PercussionTrack *>(track)) continue; // drum hits are not pitches
    for (int row = 0; row < song.getArrangementLength(); row++) {
      for (auto & note : arrangement.getNotes(row, track_id)) {
        if (!note.isDefined() || note.isOff()) continue;
        notes++;
        int step = (((note.getValue() - key) % 53) + 53) % 53;
        auto degree = ratio_of_step.find(step);
        CHECK(degree != ratio_of_step.end());
        if (degree == ratio_of_step.end()) continue;
        auto correction = corrections.forNote(note.getValue());
        CHECK(correction.has_value());
        if (!correction) continue;
        // Whole cents: within half a cent of the exact ratio.
        CHECK(std::fabs(step * 1200.0 / 53 + *correction - degree->second.cents()) <= 0.5);
      }
    }
  }
  // Per track: lyre 52 (intro run and chord, 37 melody notes, outro), octave lyre 40, lyre drone 14, two aulos 37 each, reed drone 1.
  CHECK(notes == 52 + 40 + 14 + 37 * 2 + 1);
}

TEST(song_notes_load_save_and_reload) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_SONGS_DIR) + "/seikilos.xml", provider));
  CHECK(song.getSongNotes().find("West, Ancient Greek Music") != std::string::npos);

  song.setSongNotes("line one\nline two & <more>");
  auto path = std::string(TESTS_SCRATCH_DIR) + "/song_notes_round_trip.xml";
  song.save(path);
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK(reloaded.getSongNotes() == "line one\nline two & <more>");

  song.setSongNotes("");
  song.save(path);
  CHECK(reloaded.open(path, provider));
  CHECK(reloaded.getSongNotes().empty());
}

TEST(notes_lose_the_files_indentation_and_survive_a_leading_comment) {
  auto path = std::string(TESTS_SCRATCH_DIR) + "/song_notes_indented.xml";
  {
    std::ofstream out(path);
    out << "<?xml version=\"1.0\"?>\n<song temperament=\"53edo\" tempo=\"90\">\n  <notes>\n    <!-- a comment -->\n"
           "    first line\n      indented\n\n    last line   \n  </notes>\n</song>\n";
  }
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(path, provider));
  CHECK(song.getSongNotes() == "first line\n  indented\n\nlast line");
}

TEST(only_53_edo_offers_kitmum) {
  CHECK(scaleSupportsTuning(Scale::KITMUM, Tuning::EDO53));
  CHECK(!scaleSupportsTuning(Scale::KITMUM, Tuning::EDO31));
  CHECK(scaleSupportsTuning(Scale::MAJOR, Tuning::EDO31));
}
