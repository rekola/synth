#include "TestFramework.h"

#include "../src/model/Gamut.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/Scale.h"
#include "../src/model/Song.h"

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace just_intonation;
using namespace ratio_literals;

namespace {

int stepOf(Tuning tuning, std::string_view spelling) {
  int edo = edoStepsFor(tuning);
  return (((Note::stringToKey(tuning, spelling) - Note::stringToKey(tuning, "C")) % edo) + edo) % edo;
}

int keyOf(Ratio ratio, int edo) { return ((ratio.steps(edo) % edo) + edo) % edo; }

std::vector<Ratio> ratios(Scale scale) {
  std::vector<Ratio> out;
  for (auto i : scaleGamutIndices(scale)) out.push_back(gamut::kExtendedDiamond[i].ratio);
  return out;
}

struct Row {
  Ratio ratio;
  GamutSide side;
  const char * spelling;
  int step31, step53;
};

// step31 is the nearest 31-EDO step; the spellings mean it only for the first 17
// ratios (the rest share keys in 31-EDO).
bool isEarlier(Ratio r) {
  for (auto e : {1 / 1_r, 16 / 15_r, 15 / 14_r, 8 / 7_r, 7 / 6_r, 6 / 5_r, 5 / 4_r, 4 / 3_r, 7 / 5_r, 10 / 7_r, 3 / 2_r, 8 / 5_r, 5 / 3_r, 12 / 7_r, 7 / 4_r, 28 / 15_r, 15 / 8_r})
    if (r == e) return true;
  return false;
}

const std::vector<Row> kTable = {
    {1 / 1_r, GamutSide::BOTH, "C", 0, 0},
    {50 / 49_r, GamutSide::UTONAL, "D𝄫", 1, 2},
    {49 / 48_r, GamutSide::OTONAL, "D𝄫", 1, 2},
    {16 / 15_r, GamutSide::UTONAL, "D♭↓", 3, 5},
    {15 / 14_r, GamutSide::OTONAL, "D♭↓", 3, 5},
    {10 / 9_r, GamutSide::UTONAL, "D↓", 5, 8},
    {9 / 8_r, GamutSide::OTONAL, "D", 5, 9},
    {8 / 7_r, GamutSide::UTONAL, "E𝄫", 6, 10},
    {7 / 6_r, GamutSide::OTONAL, "D♯", 7, 12},
    {6 / 5_r, GamutSide::UTONAL, "E♭", 8, 14},
    {60 / 49_r, GamutSide::UTONAL, "F𝄫", 9, 15},
    {49 / 40_r, GamutSide::OTONAL, "D𝄪", 9, 16},
    {5 / 4_r, GamutSide::OTONAL, "E", 10, 17},
    {9 / 7_r, GamutSide::OTONAL, "F♭", 11, 19},
    {4 / 3_r, GamutSide::BOTH, "F", 13, 22},
    {48 / 35_r, GamutSide::UTONAL, "G𝄫", 14, 24},
    {7 / 5_r, GamutSide::OTONAL, "F♯↑", 15, 26},
    {10 / 7_r, GamutSide::UTONAL, "G♭↓", 16, 27},
    {35 / 24_r, GamutSide::OTONAL, "F𝄪", 17, 29},
    {3 / 2_r, GamutSide::BOTH, "G", 18, 31},
    {14 / 9_r, GamutSide::UTONAL, "G♯", 20, 34},
    {8 / 5_r, GamutSide::UTONAL, "A♭", 21, 36},
    {80 / 49_r, GamutSide::UTONAL, "A♭↑", 22, 37},
    {49 / 30_r, GamutSide::OTONAL, "G𝄪", 22, 38},
    {5 / 3_r, GamutSide::OTONAL, "A", 23, 39},
    {12 / 7_r, GamutSide::UTONAL, "B𝄫", 24, 41},
    {7 / 4_r, GamutSide::OTONAL, "A♯↑", 25, 43},
    {16 / 9_r, GamutSide::UTONAL, "B♭↓", 26, 44},
    {9 / 5_r, GamutSide::OTONAL, "B♭", 26, 45},
    {28 / 15_r, GamutSide::UTONAL, "B", 28, 48},
    {15 / 8_r, GamutSide::OTONAL, "B", 28, 48},
    {96 / 49_r, GamutSide::UTONAL, "B♯", 30, 51},
    {49 / 25_r, GamutSide::OTONAL, "B♯", 30, 51},
};

} // namespace

TEST(the_extended_diamond_has_the_33_ratios_with_spellings_sides_and_keys) {
  CHECK(gamut::kExtendedDiamond.size() == 33);
  CHECK(gamut::kExtendedDiamond.size() == kTable.size());
  for (size_t i = 0; i < std::min(kTable.size(), gamut::kExtendedDiamond.size()); i++) {
    auto & note = gamut::kExtendedDiamond[i];
    CHECK(note.ratio == kTable[i].ratio);
    CHECK(note.side == kTable[i].side);
    CHECK(note.spelling == kTable[i].spelling);
    CHECK(note.ratio.steps(31) == kTable[i].step31);
    CHECK(note.ratio.steps(53) == kTable[i].step53);
    // The spelling names that key in 53-EDO.
    CHECK(stepOf(Tuning::EDO53, note.spelling) == kTable[i].step53);
  }
  // In 31-EDO the spellings of the 17 earlier ratios mean their keys too.
  for (auto & row : kTable)
    if (isEarlier(row.ratio)) CHECK(stepOf(Tuning::EDO31, row.spelling) == row.step31);
}

TEST(the_extended_diamond_is_closed_under_r_to_2_over_r_and_the_mirror_swaps_sides) {
  for (size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
    auto & note = gamut::kExtendedDiamond[i];
    CHECK(note.mirror < gamut::kExtendedDiamond.size());
    if (note.mirror >= gamut::kExtendedDiamond.size()) continue;
    auto & other = gamut::kExtendedDiamond[note.mirror];
    CHECK(other.mirror == i);
    CHECK(other.ratio == note.ratio.mirror());
    if (note.side == GamutSide::OTONAL) CHECK(other.side == GamutSide::UTONAL);
    if (note.side == GamutSide::UTONAL) CHECK(other.side == GamutSide::OTONAL);
    if (note.side == GamutSide::BOTH) CHECK(other.side == GamutSide::BOTH);
  }
  CHECK(gamut::mirrorsAreConsistent(gamut::kExtendedDiamond));
}

TEST(otonal_and_utonal_have_exactly_their_eighteen_notes) {
  CHECK((ratios(Scale::OTONAL) == std::vector<Ratio>{1 / 1_r, 49 / 48_r, 15 / 14_r, 9 / 8_r, 7 / 6_r, 49 / 40_r, 5 / 4_r, 9 / 7_r, 4 / 3_r, 7 / 5_r, 35 / 24_r, 3 / 2_r, 49 / 30_r, 5 / 3_r, 7 / 4_r, 9 / 5_r, 15 / 8_r, 49 / 25_r}));
  CHECK((ratios(Scale::UTONAL) == std::vector<Ratio>{1 / 1_r, 50 / 49_r, 16 / 15_r, 10 / 9_r, 8 / 7_r, 6 / 5_r, 60 / 49_r, 4 / 3_r, 48 / 35_r, 10 / 7_r, 3 / 2_r, 14 / 9_r, 8 / 5_r, 80 / 49_r, 12 / 7_r, 16 / 9_r, 28 / 15_r, 96 / 49_r}));
  auto names = [](Scale scale) {
    std::vector<std::string> out;
    for (auto name : scaleDegreeNames(scale)) out.emplace_back(name);
    return out;
  };
  CHECK((names(Scale::OTONAL) == std::vector<std::string>{"C", "D𝄫", "D♭↓", "D", "D♯", "D𝄪", "E", "F♭", "F", "F♯↑", "F𝄪", "G", "G𝄪", "A", "A♯↑", "B♭", "B", "B♯"}));
  CHECK((names(Scale::UTONAL) == std::vector<std::string>{"C", "D𝄫", "D♭↓", "D↓", "E𝄫", "E♭", "F𝄫", "F", "G𝄫", "G♭↓", "G", "G♯", "A♭", "A♭↑", "B𝄫", "B♭↓", "B", "B♯"}));
  // Every note of a scale has a key of its own in 53-EDO.
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    std::set<int> keys;
    for (auto r : ratios(scale)) keys.insert(keyOf(r, 53));
    CHECK(keys.size() == 18);
  }
}

TEST(utonal_is_the_mirror_of_otonal) {
  auto otonal = ratios(Scale::OTONAL);
  auto utonal = ratios(Scale::UTONAL);
  CHECK(otonal.size() == utonal.size());
  std::set<std::pair<long, long>> have;
  for (auto r : utonal) have.insert({r.num, r.den});
  for (auto r : otonal) {
    auto m = r.mirror();
    CHECK(have.count({m.num, m.den}) == 1);
  }
}

TEST(the_removed_scales_are_gone) {
  CHECK(scaleFromString("diamond7") == Scale::NONE);
  CHECK(scaleFromString(to_string(Scale::OTONAL)) == Scale::OTONAL);
  CHECK(scaleFromString(to_string(Scale::UTONAL)) == Scale::UTONAL);
}

TEST(the_two_scales_step_through_eighteen_degrees_in_53_edo) {
  auto steps = [](Tuning tuning, Scale scale) {
    std::vector<int> out;
    for (auto i : scaleGamutIndices(scale)) out.push_back(gamut::kExtendedDiamond[i].ratio.steps(edoStepsFor(tuning)));
    return out;
  };
  CHECK((steps(Tuning::EDO53, Scale::OTONAL) == std::vector<int>{0, 2, 5, 9, 12, 16, 17, 19, 22, 26, 29, 31, 38, 39, 43, 45, 48, 51}));
  CHECK((steps(Tuning::EDO53, Scale::UTONAL) == std::vector<int>{0, 2, 5, 8, 10, 14, 15, 22, 24, 27, 31, 34, 36, 37, 41, 44, 48, 51}));
  // The Launchpad's step view reads these: a window of the song's scale
  // degrees, one octave up from the eighteenth.
  Song song(Tuning::EDO53, -1);
  song.setScale(Scale::OTONAL);
  auto window = song.getScaleDegreesWindow(0, 19);
  CHECK(window.size() == 19);
  CHECK(window.front() == 0 && window[17] == 51 && window[18] == 53);
}

TEST(no_gamut_ratio_is_more_than_11_cents_from_its_key_in_53_edo_and_the_first_17_within_7) {
  for (size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
    auto & note = gamut::kExtendedDiamond[i];
    double off = std::fabs(note.ratio.cents() - note.ratio.steps(53) * 1200.0 / 53);
    CHECK(off < 11.0);
    // The 17 ratios of the 10-note scales are within 7 cents in 31 and 53-EDO.
    if (isEarlier(note.ratio)) {
      for (int edo : {31, 53}) CHECK(std::fabs(note.ratio.cents() - note.ratio.steps(edo) * 1200.0 / edo) <= 7.0);
    }
  }
}

TEST(with_just_intonation_every_gamut_note_sounds_at_its_exact_ratio_on_its_key) {
  const int edo = 53;
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    auto corrections = scaleCorrectionsFor(Tuning::EDO53, scale, 0);
    for (auto & note : gamut::kExtendedDiamond) {
      // A key that two ratios share carries the scale's own.
      bool shared = false;
      for (auto & other : gamut::kExtendedDiamond) shared = shared || (&other != &note && keyOf(other.ratio, edo) == keyOf(note.ratio, edo));
      bool own = note.side == GamutSide::BOTH || (note.side == GamutSide::OTONAL) == (scale == Scale::OTONAL);
      if (shared && !own) continue;
      auto cents = corrections.forNote(keyOf(note.ratio, edo));
      CHECK(cents && *cents == std::lround(note.ratio.cents() - note.ratio.steps(edo) * 1200.0 / edo));
    }
  }
}

TEST(keys_2_5_48_and_51_follow_the_songs_scale) {
  auto cents = [](Ratio r) { return static_cast<int>(std::lround(r.cents() - r.steps(53) * 1200.0 / 53)); };
  auto otonal = scaleCorrectionsFor(Tuning::EDO53, Scale::OTONAL, 0);
  auto utonal = scaleCorrectionsFor(Tuning::EDO53, Scale::UTONAL, 0);
  CHECK(otonal.forNote(2) == std::optional<int>(cents(49 / 48_r)));
  CHECK(utonal.forNote(2) == std::optional<int>(cents(50 / 49_r)));
  CHECK(otonal.forNote(5) == std::optional<int>(cents(15 / 14_r)));
  CHECK(utonal.forNote(5) == std::optional<int>(cents(16 / 15_r)));
  CHECK(otonal.forNote(48) == std::optional<int>(cents(15 / 8_r)));
  CHECK(utonal.forNote(48) == std::optional<int>(cents(28 / 15_r)));
  CHECK(otonal.forNote(51) == std::optional<int>(cents(49 / 25_r)));
  CHECK(utonal.forNote(51) == std::optional<int>(cents(96 / 49_r)));
  // Exactly these four keys are shared.
  std::map<int, int> count;
  for (auto & note : gamut::kExtendedDiamond) count[keyOf(note.ratio, 53)]++;
  std::set<int> shared;
  for (auto [key, n] : count)
    if (n > 1) shared.insert(key);
  CHECK((shared == std::set<int>{2, 5, 48, 51}));
  CHECK(count.size() == 29);
}

TEST(a_new_song_starts_in_53_edo) {
  Song song;
  CHECK(song.getTuning() == Tuning::EDO53);
}

TEST(in_31_edo_the_scales_still_choose_the_ratio_of_a_shared_key) {
  auto otonal = scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, 0);
  auto utonal = scaleCorrectionsFor(Tuning::EDO31, Scale::UTONAL, 0);
  CHECK(otonal.forNote(3) == std::optional<int>(3));   // 15/14
  CHECK(utonal.forNote(3) == std::optional<int>(-4));  // 16/15
  CHECK(otonal.forNote(28) == std::optional<int>(4));  // 15/8
  CHECK(utonal.forNote(28) == std::optional<int>(-3)); // 28/15
}
