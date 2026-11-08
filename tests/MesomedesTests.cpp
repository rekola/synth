#include "TestFramework.h"

#include "../src/instruments/InstrumentProvider.h"
#include "../src/model/Gamut.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/Scale.h"
#include "../src/model/Song.h"

#include <cmath>
#include <map>
#include <string>

#ifndef TESTS_SONGS_DIR
#define TESTS_SONGS_DIR "."
#endif

using namespace ratio_literals;

namespace {
constexpr int kIntroRows = 16; // two bars of 2/4
constexpr int kSongRows = 120; // twelve bars of 2/4 and one of 12/8
constexpr int kOutroRows = 16;
} // namespace

TEST(nid_qablim_is_the_phrygian_rotation_of_the_pythagorean_gamut) {
  std::vector<Ratio> ratios;
  for (auto i : scaleGamutIndices(Scale::NID_QABLIM)) ratios.push_back(gamut::kPythagorean[i].ratio);
  CHECK((ratios == std::vector<Ratio>{1 / 1_r, 256 / 243_r, 32 / 27_r, 4 / 3_r, 3 / 2_r, 128 / 81_r, 16 / 9_r}));
  CHECK(scaleFromString(to_string(Scale::NID_QABLIM)) == Scale::NID_QABLIM);
  CHECK(scaleGamut(Scale::NID_QABLIM).data() == gamut::kPythagorean.data());
  CHECK(scaleSupportsTuning(Scale::NID_QABLIM, Tuning::EDO53));
  CHECK(!scaleSupportsTuning(Scale::NID_QABLIM, Tuning::EDO31));
}

TEST(mesomedes_plays_scale_notes_tuned_to_their_ratios_in_120_rows) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_SONGS_DIR) + "/mesomedes_calliope.xml", provider));
  CHECK(song.getTuning() == Tuning::EDO53);
  CHECK(song.getScale() == Scale::NID_QABLIM);
  CHECK(song.getArrangementLength() == kIntroRows + kSongRows + kOutroRows);
  // Bar 13 is one 12/8 bar, between the 2/4 of the rest.
  auto bars = song.getArrangementBars();
  CHECK(bars.barRows(kIntroRows) == 8 && bars.barRows(kIntroRows + 96) == 24 && bars.barRows(kIntroRows + kSongRows) == 8);
  CHECK(bars.barIndex(kIntroRows + kSongRows) == 2 + 13 && bars.barIndex(kIntroRows + kSongRows + kOutroRows) == 2 + 13 + 2);

  // The tempo broadens over the outro, as tempo changes in the arrangement.
  CHECK((song.getTempoChanges() == std::map<int, int>{{136, 75}, {140, 70}, {144, 66}, {148, 64}}));

  const int key = song.getKey();
  CHECK(key == Note::stringToKey(Tuning::EDO53, "C-4"));
  auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::NID_QABLIM, key);

  auto step_of = [](Ratio r) { return ((r.steps(53) % 53) + 53) % 53; };
  std::map<int, Ratio> ratio_of_step; // the scale's degrees
  for (auto i : scaleGamutIndices(Scale::NID_QABLIM)) ratio_of_step.emplace(step_of(gamut::kPythagorean[i].ratio), gamut::kPythagorean[i].ratio);

  // The one note off the scale is West's g-sharp in bar 4, the Pythagorean 81/64.
  const int ficta_row = kIntroRows + 24;
  const Ratio ficta = 81 / 64_r;

  int off_scale = 0;
  auto arrangement = song.getArrangement();
  for (auto & [track_id, pattern] : arrangement.getPatternsByTrack()) {
    auto * track = song.getMasterTrack().getChildByInternalId(track_id);
    if (!track || dynamic_cast<const PercussionTrack *>(track)) continue; // drum hits are not pitches
    for (int row = 0; row < song.getArrangementLength(); row++) {
      for (auto & note : arrangement.getNotes(row, track_id)) {
        if (!note.isDefined() || note.isOff()) continue;
        int step = (((note.getValue() - key) % 53) + 53) % 53;
        bool ficta_note = row == ficta_row && step == step_of(ficta);
        auto degree = ficta_note ? std::map<int, Ratio>::iterator{} : ratio_of_step.find(step);
        if (!ficta_note) {
          CHECK(degree != ratio_of_step.end());
          if (degree == ratio_of_step.end()) continue;
        }
        if (ficta_note) off_scale++;
        Ratio expected = ficta_note ? ficta : degree->second;
        auto correction = corrections.forNote(note.getValue());
        CHECK(correction.has_value());
        if (!correction) continue;
        CHECK(std::fabs(step * 1200.0 / 53 + *correction - expected.cents()) <= 0.5);
      }
    }
  }
  CHECK(off_scale == 1); // the lyre alone
}
