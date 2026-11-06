#include "TestFramework.h"

#include "../src/instruments/InstrumentProvider.h"
#include "../src/model/Gamut.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/Scale.h"
#include "../src/model/Song.h"

#include <cmath>
#include <map>
#include <string>

#ifndef TESTS_SONGS_DIR
#define TESTS_SONGS_DIR "."
#endif

using namespace ratio_literals;

TEST(the_mixolydian_scale_is_c_d_e_f_g_a_b_flat_of_the_pythagorean_gamut) {
  std::vector<Ratio> ratios;
  for (auto i : scaleGamutIndices(Scale::MIXOLYDIAN)) ratios.push_back(gamut::kPythagorean[i].ratio);
  CHECK((ratios == std::vector<Ratio>{1 / 1_r, 9 / 8_r, 81 / 64_r, 4 / 3_r, 3 / 2_r, 27 / 16_r, 16 / 9_r}));
  CHECK(scaleFromString(to_string(Scale::MIXOLYDIAN)) == Scale::MIXOLYDIAN);
  // Every gamut note is 3-limit and sits on its 53-EDO spelling's step.
  for (auto & note : gamut::kPythagorean) {
    CHECK(Note::stringToKey(Tuning::EDO53, std::string(note.spelling) + "4") - Note::stringToKey(Tuning::EDO53, "C-4") ==
          ((note.ratio.steps(53) % 53) + 53) % 53);
  }
}

TEST(seikilos_plays_only_scale_notes_each_tuned_to_its_3_limit_ratio_in_96_rows) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_SONGS_DIR) + "/seikilos.xml", provider));
  CHECK(song.getTuning() == Tuning::EDO53);
  CHECK(song.getScale() == Scale::MIXOLYDIAN);
  CHECK(song.getArrangementLength() == 96);

  const int key = song.getKey();
  CHECK(key == Note::stringToKey(Tuning::EDO53, "C-4"));
  auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::MIXOLYDIAN, key);

  std::map<int, Ratio> ratio_of_step; // the scale's degrees
  for (auto i : scaleGamutIndices(Scale::MIXOLYDIAN)) ratio_of_step.emplace(((gamut::kPythagorean[i].ratio.steps(53) % 53) + 53) % 53, gamut::kPythagorean[i].ratio);

  int notes = 0;
  auto arrangement = song.getArrangement();
  for (auto & [track_id, pattern] : arrangement.getPatternsByTrack()) {
    for (int row = 0; row < 96; row++) {
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
  // 8 bars: 3 + 4 + 5 + 4 + 6 + 4 + 6 + 5 melody notes, twice, plus 4 x 2 drone notes.
  CHECK(notes == 37 * 2 + 8);
}
