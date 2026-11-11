#include "TestFramework.h"

#include "../src/model/Gamut.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/Scale.h"

#include <cmath>
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

const std::vector<Row> kTable = {
    {1 / 1_r, GamutSide::BOTH, "C", 0, 0},
    {16 / 15_r, GamutSide::UTONAL, "D♭↓", 3, 5},
    {15 / 14_r, GamutSide::OTONAL, "D♭↓", 3, 5},
    {8 / 7_r, GamutSide::UTONAL, "E𝄫", 6, 10},
    {7 / 6_r, GamutSide::OTONAL, "D♯", 7, 12},
    {6 / 5_r, GamutSide::UTONAL, "E♭", 8, 14},
    {5 / 4_r, GamutSide::OTONAL, "E", 10, 17},
    {4 / 3_r, GamutSide::BOTH, "F", 13, 22},
    {7 / 5_r, GamutSide::OTONAL, "F♯↑", 15, 26},
    {10 / 7_r, GamutSide::UTONAL, "G♭↓", 16, 27},
    {3 / 2_r, GamutSide::BOTH, "G", 18, 31},
    {8 / 5_r, GamutSide::UTONAL, "A♭", 21, 36},
    {5 / 3_r, GamutSide::OTONAL, "A", 23, 39},
    {12 / 7_r, GamutSide::UTONAL, "B𝄫", 24, 41},
    {7 / 4_r, GamutSide::OTONAL, "A♯↑", 25, 43},
    {28 / 15_r, GamutSide::UTONAL, "B", 28, 48},
    {15 / 8_r, GamutSide::OTONAL, "B", 28, 48},
};

} // namespace

TEST(the_extended_diamond_has_the_seventeen_ratios_with_spellings_sides_and_steps) {
  CHECK(gamut::kExtendedDiamond.size() == kTable.size());
  for (size_t i = 0; i < std::min(kTable.size(), gamut::kExtendedDiamond.size()); i++) {
    auto & note = gamut::kExtendedDiamond[i];
    CHECK(note.ratio == kTable[i].ratio);
    CHECK(note.side == kTable[i].side);
    CHECK(note.spelling == kTable[i].spelling);
    CHECK(note.ratio.steps(31) == kTable[i].step31);
    CHECK(note.ratio.steps(53) == kTable[i].step53);
    // The spelling names that key in both tunings.
    CHECK(stepOf(Tuning::EDO31, note.spelling) == kTable[i].step31);
    CHECK(stepOf(Tuning::EDO53, note.spelling) == kTable[i].step53);
  }
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

TEST(otonal_and_utonal_have_exactly_their_ten_notes) {
  CHECK((ratios(Scale::OTONAL) == std::vector<Ratio>{1 / 1_r, 15 / 14_r, 7 / 6_r, 5 / 4_r, 4 / 3_r, 7 / 5_r, 3 / 2_r, 5 / 3_r, 7 / 4_r, 15 / 8_r}));
  CHECK((ratios(Scale::UTONAL) == std::vector<Ratio>{1 / 1_r, 16 / 15_r, 8 / 7_r, 6 / 5_r, 4 / 3_r, 10 / 7_r, 3 / 2_r, 8 / 5_r, 12 / 7_r, 28 / 15_r}));
  auto names = [](Scale scale) {
    std::vector<std::string> out;
    for (auto name : scaleDegreeNames(scale)) out.emplace_back(name);
    return out;
  };
  CHECK((names(Scale::OTONAL) == std::vector<std::string>{"C", "D♭↓", "D♯", "E", "F", "F♯↑", "G", "A", "A♯↑", "B"}));
  CHECK((names(Scale::UTONAL) == std::vector<std::string>{"C", "D♭↓", "E𝄫", "E♭", "F", "G♭↓", "G", "A♭", "B𝄫", "B"}));
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

TEST(the_two_scales_step_through_ten_degrees_in_31_and_53_edo) {
  auto steps = [](Tuning tuning, Scale scale) {
    std::vector<int> out;
    for (auto i : scaleGamutIndices(scale)) out.push_back(gamut::kExtendedDiamond[i].ratio.steps(edoStepsFor(tuning)));
    return out;
  };
  CHECK((steps(Tuning::EDO31, Scale::OTONAL) == std::vector<int>{0, 3, 7, 10, 13, 15, 18, 23, 25, 28}));
  CHECK((steps(Tuning::EDO31, Scale::UTONAL) == std::vector<int>{0, 3, 6, 8, 13, 16, 18, 21, 24, 28}));
  CHECK((steps(Tuning::EDO53, Scale::OTONAL) == std::vector<int>{0, 5, 12, 17, 22, 26, 31, 39, 43, 48}));
  CHECK((steps(Tuning::EDO53, Scale::UTONAL) == std::vector<int>{0, 5, 10, 14, 22, 27, 31, 36, 41, 48}));
}

TEST(no_gamut_ratio_is_more_than_6_2_cents_from_its_key_in_31_or_53_edo) {
  for (auto tuning : {Tuning::EDO31, Tuning::EDO53}) {
    int edo = edoStepsFor(tuning);
    for (auto & note : gamut::kExtendedDiamond) {
      double off = std::fabs(note.ratio.cents() - note.ratio.steps(edo) * 1200.0 / edo);
      CHECK(off <= 6.2);
    }
  }
}

TEST(with_just_intonation_every_gamut_note_sounds_at_its_exact_ratio_on_its_key) {
  for (auto tuning : {Tuning::EDO31, Tuning::EDO53}) {
    int edo = edoStepsFor(tuning);
    for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
      auto corrections = scaleCorrectionsFor(tuning, scale, 0);
      for (auto & note : gamut::kExtendedDiamond) {
        // The two shared keys carry the scale's own ratio.
        bool shared = note.ratio == 16 / 15_r || note.ratio == 15 / 14_r || note.ratio == 28 / 15_r || note.ratio == 15 / 8_r;
        bool own = note.side == GamutSide::BOTH || (note.side == GamutSide::OTONAL) == (scale == Scale::OTONAL);
        if (shared && !own) continue;
        auto cents = corrections.forNote(keyOf(note.ratio, edo));
        CHECK(cents && *cents == std::lround(note.ratio.cents() - note.ratio.steps(edo) * 1200.0 / edo));
      }
    }
  }
}

TEST(the_two_shared_keys_follow_the_songs_scale) {
  auto cents = [](Ratio r, int edo) { return static_cast<int>(std::lround(r.cents() - r.steps(edo) * 1200.0 / edo)); };
  for (auto [tuning, edo] : {std::pair{Tuning::EDO31, 31}, std::pair{Tuning::EDO53, 53}}) {
    auto otonal = scaleCorrectionsFor(tuning, Scale::OTONAL, 0);
    auto utonal = scaleCorrectionsFor(tuning, Scale::UTONAL, 0);
    CHECK(otonal.forNote(keyOf(15 / 14_r, edo)) == std::optional<int>(cents(15 / 14_r, edo)));
    CHECK(otonal.forNote(keyOf(15 / 8_r, edo)) == std::optional<int>(cents(15 / 8_r, edo)));
    CHECK(utonal.forNote(keyOf(16 / 15_r, edo)) == std::optional<int>(cents(16 / 15_r, edo)));
    CHECK(utonal.forNote(keyOf(28 / 15_r, edo)) == std::optional<int>(cents(28 / 15_r, edo)));
  }
}

TEST(31_and_53_edo_have_fifteen_keys_for_the_gamut) {
  for (auto tuning : {Tuning::EDO31, Tuning::EDO53}) {
    int edo = edoStepsFor(tuning);
    std::set<int> keys;
    for (auto & note : gamut::kExtendedDiamond) keys.insert(keyOf(note.ratio, edo));
    CHECK(keys.size() == 15);
    for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
      auto corrections = scaleCorrectionsFor(tuning, scale, 0);
      int present = 0;
      for (int step = 0; step < edo; step++) present += corrections.forNote(step) ? 1 : 0;
      CHECK(present == 15);
    }
  }
}
