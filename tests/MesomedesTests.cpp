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

TEST(nid_qablim_is_the_phrygian_rotation_of_the_pythagorean_gamut) {
  std::vector<Ratio> ratios;
  for (auto i : scaleGamutIndices(Scale::NID_QABLIM)) ratios.push_back(gamut::kPythagorean[i].ratio);
  CHECK((ratios == std::vector<Ratio>{1 / 1_r, 256 / 243_r, 32 / 27_r, 4 / 3_r, 3 / 2_r, 128 / 81_r, 16 / 9_r}));
  CHECK(scaleFromString(to_string(Scale::NID_QABLIM)) == Scale::NID_QABLIM);
  CHECK(scaleGamut(Scale::NID_QABLIM).data() == gamut::kPythagorean.data());
  CHECK(scaleSupportsTuning(Scale::NID_QABLIM, Tuning::EDO53));
  CHECK(!scaleSupportsTuning(Scale::NID_QABLIM, Tuning::EDO31));
}

TEST(the_hurrian_hymn_plays_scale_notes_tuned_to_their_ratios) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_SONGS_DIR) + "/hurrian_nikkal.xml", provider));
  CHECK(song.getTuning() == Tuning::EDO53);
  CHECK(song.getScale() == Scale::NID_QABLIM);
  CHECK(song.getArrangementLength() > 0);

  const int key = song.getKey();
  CHECK(key == Note::stringToKey(Tuning::EDO53, "D-4"));
  auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::NID_QABLIM, key);

  auto step_of = [](Ratio r) { return ((r.steps(53) % 53) + 53) % 53; };
  std::map<int, Ratio> ratio_of_step; // the scale's degrees
  for (auto i : scaleGamutIndices(Scale::NID_QABLIM)) ratio_of_step.emplace(step_of(gamut::kPythagorean[i].ratio), gamut::kPythagorean[i].ratio);

  int pitched = 0;
  auto arrangement = song.getArrangement();
  for (auto & [track_id, pattern] : arrangement.getPatternsByTrack()) {
    auto * track = song.getMasterTrack().getChildByInternalId(track_id);
    if (!track || dynamic_cast<const PercussionTrack *>(track)) continue; // drum hits are not pitches
    for (int row = 0; row < song.getArrangementLength(); row++) {
      for (auto & note : arrangement.getNotes(row, track_id)) {
        if (!note.isDefined() || note.isOff()) continue;
        pitched++;
        int step = (((note.getValue() - key) % 53) + 53) % 53;
        auto degree = ratio_of_step.find(step);
        CHECK(degree != ratio_of_step.end()); // every note is a degree of the tuning
        if (degree == ratio_of_step.end()) continue;
        auto correction = corrections.forNote(note.getValue());
        CHECK(correction.has_value());
        if (!correction) continue;
        CHECK(std::fabs(step * 1200.0 / 53 + *correction - degree->second.cents()) <= 0.5);
      }
    }
  }
  CHECK(pitched > 0);
}
