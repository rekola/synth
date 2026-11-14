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
#include <vector>

#ifndef TESTS_SONGS_DIR
#define TESTS_SONGS_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace ratio_literals;

namespace {
int stepOf(Ratio r) { return ((r.steps(53) % 53) + 53) % 53; }

// (53-EDO step above the root, ratio) of the ten degrees.
const std::vector<std::pair<int, Ratio>> kDegrees = {
    {0, 1 / 1_r}, {4, 256 / 243_r}, {9, 9 / 8_r}, {13, 32 / 27_r}, {22, 4 / 3_r}, {26, 1024 / 729_r}, {31, 3 / 2_r}, {35, 128 / 81_r}, {40, 27 / 16_r}, {49, 243 / 128_r}};
} // namespace

TEST(phrygian_tonos_parses_and_round_trips) {
  CHECK(scaleFromString("phrygian-tonos") == Scale::PHRYGIAN_TONOS);
  CHECK(to_string(Scale::PHRYGIAN_TONOS) == "phrygian-tonos");
  CHECK(scaleLabel(Scale::PHRYGIAN_TONOS) == "Phrygian tonos");
  CHECK(scaleGamut(Scale::PHRYGIAN_TONOS).data() == gamut::kPythagorean.data());
  CHECK(scaleSupportsTuning(Scale::PHRYGIAN_TONOS, Tuning::EDO53));
  CHECK(!scaleSupportsTuning(Scale::PHRYGIAN_TONOS, Tuning::EDO31));

  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_SONGS_DIR) + "/athenaeus_paean.xml", provider));
  CHECK(song.getScale() == Scale::PHRYGIAN_TONOS);
  auto path = std::string(TESTS_SCRATCH_DIR) + "/phrygian_tonos_round_trip.xml";
  song.save(path);
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK(reloaded.getScale() == Scale::PHRYGIAN_TONOS);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(text.find("scale=\"phrygian-tonos\"") != std::string::npos);
}

TEST(phrygian_tonos_degrees_are_steps_and_ratios_above_the_key) {
  std::vector<Ratio> ratios;
  for (auto i : scaleGamutIndices(Scale::PHRYGIAN_TONOS)) ratios.push_back(gamut::kPythagorean[i].ratio);
  CHECK(ratios.size() == kDegrees.size());
  for (size_t i = 0; i < ratios.size() && i < kDegrees.size(); i++) {
    CHECK(ratios[i] == kDegrees[i].second);
    CHECK(stepOf(ratios[i]) == kDegrees[i].first);
  }
  for (auto key_name : {"C-4", "D-4", "A↑3"}) {
    int key = Note::stringToKey(Tuning::EDO53, key_name);
    auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::PHRYGIAN_TONOS, key);
    for (auto & [step, ratio] : kDegrees) {
      int value = key + step;
      auto correction = corrections.forNote(value);
      CHECK(correction.has_value());
      if (!correction) continue;
      CHECK(std::fabs(step * 1200.0 / 53 + *correction - ratio.cents()) <= 0.5);
      // Every ratio is within 0.5 cent of its step, so 53-EDO needs no correction.
      CHECK(*correction == 0);
    }
  }
}

TEST(the_athenaeus_paean_plays_only_scale_notes_tuned_to_their_ratios) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_SONGS_DIR) + "/athenaeus_paean.xml", provider));
  CHECK(song.getTuning() == Tuning::EDO53);
  const int key = song.getKey();
  CHECK(key == Note::stringToKey(Tuning::EDO53, "C-4"));
  auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::PHRYGIAN_TONOS, key);
  std::map<int, Ratio> ratio_of_step(kDegrees.begin(), kDegrees.end());

  auto check_note = [&](const Note & note) {
    if (!note.isNoteOn()) return; // OFF, ??? and ~~~ are not notes
    int step = (((note.getValue() - key) % 53) + 53) % 53;
    auto degree = ratio_of_step.find(step);
    CHECK(degree != ratio_of_step.end());
    if (degree == ratio_of_step.end()) return;
    if (note.getFx().substr(0, 1) == "G") return; // glides carry no correction: tuned by their step
    auto correction = corrections.forNote(note.getValue());
    CHECK(correction.has_value());
    if (!correction) return;
    CHECK(std::fabs(step * 1200.0 / 53 + *correction - degree->second.cents()) <= 0.5);
    CHECK(note.getTuningCorrectionCents() == *correction);
  };

  int pitched = 0;
  for (auto & track : song.getMasterTrack().getChildren()) {
    if (dynamic_cast<const PercussionTrack *>(track.get())) continue;
    for (auto clip : song.getClips(track->getInternalId())) {
      CHECK(clip.getLength() % 10 == 0);
      for (auto & [row, notes] : clip.getLeafPattern().getNotesByRow()) {
        for (auto & note : notes) {
          if (note.isNoteOn()) pitched++;
          check_note(note);
        }
      }
    }
  }
  CHECK(pitched > 0);
}

TEST(section_one_first_chorus_clip_has_its_100_notes) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_SONGS_DIR) + "/athenaeus_paean.xml", provider));
  auto clips = song.getClips(song.getMasterTrack().getChildren()[0]->getInternalId());
  CHECK(clips.size() >= 2);
  auto clip = clips[1];
  CHECK(clip.getName() == "Section 1");
  CHECK(clip.getLength() == 280);
  int count = 0, last_row = -1, last_value = -1;
  for (auto & [row, notes] : clip.getLeafPattern().getNotesByRow()) {
    for (auto & note : notes) {
      if (!note.isNoteOn()) continue;
      count++;
      last_row = row;
      last_value = note.getValue();
    }
  }
  CHECK(count == 100);
  CHECK(last_row == 276);
  CHECK(last_value == Note::stringToKey(Tuning::EDO53, "G-3"));
}
