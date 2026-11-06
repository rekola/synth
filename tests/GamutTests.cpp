#include "TestFramework.h"

#include "../src/model/Gamut.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/Scale.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

using namespace just_intonation;
using namespace ratio_literals;

namespace {

bool isTwoTimesThreeSmooth(long n) {
  while (n > 0 && n % 2 == 0) n /= 2;
  if (n % 3 == 0) n /= 3;
  return n == 1;
}

// A harmonic of the F a fifth below C: (k+1)/k with k = 2^a*3^b (b <= 1), or
// 2k/(k+1) with k+1 = 2^a*3^b (b <= 1).
bool isOtonalSeries(Ratio r) {
  for (long n = 1; n <= 4096; n++) {
    if (isTwoTimesThreeSmooth(n) && r == Ratio{n + 1, n}) return true;
    if (n >= 2 && isTwoTimesThreeSmooth(n) && r == Ratio{2 * (n - 1), n}) return true;
  }
  return false;
}

Ratio arithmeticMean(Ratio a, Ratio b) { return Ratio{std::int64_t{a.num} * b.den + std::int64_t{b.num} * a.den, 2L * a.den * b.den}; }
Ratio harmonicMean(Ratio a, Ratio b) { return Ratio{2L * a.num * b.num, std::int64_t{a.num} * b.den + std::int64_t{b.num} * a.den}; }

struct Generated {
  Ratio ratio;
  GamutSide side;
  int round;
};

bool contains(const std::vector<Generated> & notes, Ratio r) {
  return std::any_of(notes.begin(), notes.end(), [&](const Generated & n) { return n.ratio == r; });
}

// The rule documented in Gamut.h, applied literally, up to `rounds`.
std::vector<Generated> regenerate(int rounds) {
  const Ratio one{1, 1}, octave{2, 1};
  std::vector<Generated> out = {{one, GamutSide::BOTH, 0}};
  if (rounds < 1) return out;
  out.push_back({arithmeticMean(one, octave), GamutSide::BOTH, 1});
  out.push_back({harmonicMean(one, octave), GamutSide::BOTH, 1});
  for (int round = 2; round <= rounds; round++) {
    std::vector<Ratio> previous;
    for (auto & note : out)
      if (note.round == round - 1) previous.push_back(note.ratio);
    for (auto x : previous) {
      for (auto [low, high] : {std::pair{one, x}, std::pair{x, octave}}) {
        auto arithmetic = arithmeticMean(low, high);
        if (isOtonalSeries(arithmetic) && !contains(out, arithmetic)) out.push_back({arithmetic, GamutSide::OTONAL, round});
        auto harmonic = harmonicMean(low, high);
        if (isOtonalSeries(Ratio{2 * std::int64_t{harmonic.den}, harmonic.num}) && !contains(out, harmonic))
          out.push_back({harmonic, GamutSide::UTONAL, round});
      }
    }
  }
  return out;
}

std::set<std::size_t> setOf(const std::vector<std::size_t> & v) { return {v.begin(), v.end()}; }

int stepOf(Tuning tuning, std::string_view spelling) {
  int edo = edoStepsFor(tuning);
  return (((Note::stringToKey(tuning, spelling) - Note::stringToKey(tuning, "C")) % edo) + edo) % edo;
}

} // namespace

TEST(regenerating_the_gamut_from_its_rule_reproduces_the_table_in_order) {
  auto generated = regenerate(3);
  CHECK(generated.size() == gamut::kSuperparticular.size());
  for (size_t i = 0; i < std::min(generated.size(), gamut::kSuperparticular.size()); i++) {
    CHECK(generated[i].ratio == gamut::kSuperparticular[i].ratio);
    CHECK(generated[i].side == gamut::kSuperparticular[i].side);
    CHECK(generated[i].round == gamut::kSuperparticular[i].round);
  }
  // Round 4 adds the four notes and mirrors the table's comment names.
  std::set<std::pair<int, int>> round4;
  for (auto & note : regenerate(4))
    if (note.round == 4) round4.insert({note.ratio.num, note.ratio.den});
  CHECK((round4 == std::set<std::pair<int, int>>{{25, 24}, {17, 16}, {23, 12}, {31, 16}, {48, 25}, {32, 17}, {24, 23}, {32, 31}}));
}

TEST(every_gamut_note_has_a_mirror_that_mirrors_back) {
  for (size_t i = 0; i < gamut::kSuperparticular.size(); i++) {
    auto & note = gamut::kSuperparticular[i];
    CHECK(note.mirror < gamut::kSuperparticular.size());
    if (note.mirror >= gamut::kSuperparticular.size()) continue;
    CHECK(gamut::kSuperparticular[note.mirror].mirror == i);
    CHECK(gamut::kSuperparticular[note.mirror].ratio == note.ratio.mirror());
    CHECK(gamut::kSuperparticular[note.mirror].round == note.round);
    if (note.side != GamutSide::BOTH) CHECK(gamut::kSuperparticular[note.mirror].side != note.side);
  }
}

TEST(the_just_scales_are_selections_from_the_gamut) {
  CHECK((setOf(scaleGamutIndices(Scale::OTONAL)) == std::set<std::size_t>{0, 1, 2, 3, 5, 7, 9, 11, 13, 15, 17}));
  CHECK((setOf(scaleGamutIndices(Scale::UTONAL)) == std::set<std::size_t>{0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 18}));
  CHECK(scaleGamutIndices(Scale::MAJOR).empty());

  auto ratios = [](Scale scale) {
    std::vector<Ratio> out;
    for (auto i : scaleGamutIndices(scale)) out.push_back(gamut::kSuperparticular[i].ratio);
    return out;
  };
  // The 11-note lists the scales had before the gamut, by ascending ratio.
  CHECK((ratios(Scale::OTONAL) == std::vector<Ratio>{1 / 1_r, 13 / 12_r, 9 / 8_r, 7 / 6_r, 5 / 4_r, 4 / 3_r, 3 / 2_r, 5 / 3_r, 7 / 4_r, 11 / 6_r, 15 / 8_r}));
  CHECK((ratios(Scale::UTONAL) == std::vector<Ratio>{1 / 1_r, 16 / 15_r, 12 / 11_r, 8 / 7_r, 6 / 5_r, 4 / 3_r, 3 / 2_r, 8 / 5_r, 12 / 7_r, 16 / 9_r, 24 / 13_r}));
}

TEST(gamut_notes_map_to_their_nearest_step) {
  auto steps = [](Tuning tuning, Scale scale) {
    std::vector<int> out;
    for (auto i : scaleGamutIndices(scale)) out.push_back(gamut::kSuperparticular[i].ratio.steps(edoStepsFor(tuning)));
    return out;
  };
  CHECK((steps(Tuning::EDO53, Scale::OTONAL) == std::vector<int>{0, 6, 9, 12, 17, 22, 31, 39, 43, 46, 48}));
  CHECK((steps(Tuning::EDO53, Scale::UTONAL) == std::vector<int>{0, 5, 7, 10, 14, 22, 31, 36, 41, 44, 47}));
  CHECK((steps(Tuning::EDO31, Scale::OTONAL) == std::vector<int>{0, 4, 5, 7, 10, 13, 18, 23, 25, 27, 28}));
  CHECK((steps(Tuning::EDO31, Scale::UTONAL) == std::vector<int>{0, 3, 4, 6, 8, 13, 18, 21, 24, 26, 27}));
}

TEST(spellings_name_the_nearest_step_except_for_two_notes_in_31_edo) {
  for (auto tuning : {Tuning::EDO12, Tuning::EDO19, Tuning::EDO31, Tuning::EDO53}) {
    int edo = edoStepsFor(tuning);
    for (auto & note : gamut::kSuperparticular) {
      int nearest = ((note.ratio.steps(edo) % edo) + edo) % edo;
      bool exception = tuning == Tuning::EDO31 && (note.ratio == 13 / 12_r || note.ratio == 24 / 13_r);
      CHECK((stepOf(tuning, note.spelling) == nearest) != exception);
    }
  }
}

TEST(31_edo_collisions_resolve_to_the_songs_scale) {
  // Step 4: 13/12 (D♭) and 12/11 (C𝄪). Step 27: 11/6 (C𝄫) and 24/13 (B↓).
  auto otonal = scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, 0);
  auto utonal = scaleCorrectionsFor(Tuning::EDO31, Scale::UTONAL, 0);
  CHECK(otonal.forNote(4) == std::optional<int>(-16)); // 13/12
  CHECK(utonal.forNote(4) == std::optional<int>(-4));  // 12/11
  CHECK(otonal.forNote(27) == std::optional<int>(4));  // 11/6
  CHECK(utonal.forNote(27) == std::optional<int>(16)); // 24/13
  // No collision: the note is tuned whichever scale the song is in.
  CHECK(otonal.forNote(3) == std::optional<int>(-4)); // 16/15
  CHECK(utonal.forNote(28) == std::optional<int>(4)); // 15/8
  // A step no gamut note is nearest to stays tempered.
  CHECK(!otonal.forNote(1));
  CHECK(!utonal.forNote(2));
}

TEST(53_edo_has_no_collisions_so_every_gamut_note_is_tuned_in_either_scale) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    auto corrections = scaleCorrectionsFor(Tuning::EDO53, scale, 0);
    for (auto & note : gamut::kSuperparticular) {
      auto cents = corrections.forNote(note.ratio.steps(53));
      CHECK(cents && *cents == std::lround(note.ratio.cents() - note.ratio.steps(53) * 1200.0 / 53));
    }
  }
}

TEST(a_step_shared_by_notes_outside_the_scale_goes_to_the_scales_side_then_the_lowest_ratio) {
  // 12-EDO step 2 holds 9/8, 12/11 and 8/7. In an otonal song 9/8 is the scale's
  // own; in a utonal one 12/11 and 8/7 are both in the scale and the lower wins.
  CHECK(scaleCorrectionsFor(Tuning::EDO12, Scale::OTONAL, 0).forNote(2) == std::optional<int>(4));
  CHECK(scaleCorrectionsFor(Tuning::EDO12, Scale::UTONAL, 0).forNote(2) == std::optional<int>(-49));
}

namespace {

// n/d for n, d in {1, 3, 5, 7}, octave-reduced, by ascending ratio.
std::vector<Ratio> regenerateDiamond7() {
  std::vector<Ratio> out;
  for (int n : {1, 3, 5, 7}) {
    for (int d : {1, 3, 5, 7}) {
      Ratio r = Ratio{n, d}.reduced();
      if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

TEST(the_diamond_regenerates_from_its_rule) {
  auto expected = regenerateDiamond7();
  CHECK(expected.size() == 13);
  CHECK(gamut::kDiamond7.size() == expected.size());
  for (size_t i = 0; i < std::min(expected.size(), gamut::kDiamond7.size()); i++) CHECK(gamut::kDiamond7[i].ratio == expected[i]);
}

TEST(every_diamond_note_mirrors_to_a_note_in_the_set) {
  for (size_t i = 0; i < gamut::kDiamond7.size(); i++) {
    auto & note = gamut::kDiamond7[i];
    CHECK(note.mirror < gamut::kDiamond7.size());
    if (note.mirror >= gamut::kDiamond7.size()) continue;
    CHECK(gamut::kDiamond7[note.mirror].mirror == i);
    CHECK(gamut::kDiamond7[note.mirror].ratio == note.ratio.mirror());
  }
  // 8/7<->7/4, 7/6<->12/7, 6/5<->5/3, 5/4<->8/5, 4/3<->3/2, 7/5<->10/7.
  auto mirror_of = [](Ratio r) {
    for (auto & note : gamut::kDiamond7)
      if (note.ratio == r) return gamut::kDiamond7[note.mirror].ratio;
    return Ratio{1, 1};
  };
  CHECK(mirror_of(8 / 7_r) == 7 / 4_r);
  CHECK(mirror_of(7 / 5_r) == 10 / 7_r);
  CHECK(mirror_of(4 / 3_r) == 3 / 2_r);
}

TEST(the_diamond_scale_holds_all_thirteen_notes) {
  CHECK(scaleGamutIndices(Scale::DIAMOND7).size() == 13);
  CHECK(scaleDegreeNames(Scale::DIAMOND7).size() == 13);
  CHECK(scaleFromString(to_string(Scale::DIAMOND7)) == Scale::DIAMOND7);
  CHECK(to_string(Scale::DIAMOND7) == "diamond7");
}

TEST(the_diamond_shares_eleven_notes_with_the_superparticular_gamut_spelling_and_step) {
  int shared = 0;
  for (auto & note : gamut::kDiamond7) {
    auto other = std::find_if(gamut::kSuperparticular.begin(), gamut::kSuperparticular.end(),
                              [&](const GamutNote & n) { return n.ratio == note.ratio; });
    if (other == gamut::kSuperparticular.end()) continue;
    shared++;
    CHECK(other->spelling == note.spelling);
    for (auto tuning : {Tuning::EDO12, Tuning::EDO19, Tuning::EDO31, Tuning::EDO53}) {
      CHECK(stepOf(tuning, note.spelling) == stepOf(tuning, other->spelling));
      CHECK(note.ratio.steps(edoStepsFor(tuning)) == other->ratio.steps(edoStepsFor(tuning)));
    }
    // Tuned the same by the diamond as by whichever superparticular scale has it.
    for (auto tuning : {Tuning::EDO31, Tuning::EDO53}) {
      int step = note.ratio.steps(edoStepsFor(tuning));
      auto diamond = scaleCorrectionsFor(tuning, Scale::DIAMOND7, 0).forNote(step);
      auto super = scaleCorrectionsFor(tuning, other->side == GamutSide::UTONAL ? Scale::UTONAL : Scale::OTONAL, 0).forNote(step);
      CHECK(diamond && diamond == super);
    }
  }
  CHECK(shared == 11);
  // The two new notes: 7/5 and 10/7.
  CHECK(stepOf(Tuning::EDO31, "F♯↑") == 15);
  CHECK(stepOf(Tuning::EDO31, "G♭↓") == 16);
  CHECK(stepOf(Tuning::EDO53, "F♯↑") == 26);
  CHECK(stepOf(Tuning::EDO53, "G♭↓") == 27);
  CHECK(7 / 5_r == gamut::kDiamond7[6].ratio && gamut::kDiamond7[6].ratio.steps(31) == 15);
  CHECK(gamut::kDiamond7[7].ratio.steps(31) == 16 && gamut::kDiamond7[6].ratio.steps(53) == 26 && gamut::kDiamond7[7].ratio.steps(53) == 27);
}

TEST(the_diamond_has_no_step_collisions_in_31_or_53_edo) {
  for (auto tuning : {Tuning::EDO31, Tuning::EDO53}) {
    int edo = edoStepsFor(tuning);
    std::set<int> steps;
    for (auto & note : gamut::kDiamond7) steps.insert(((note.ratio.steps(edo) % edo) + edo) % edo);
    CHECK(steps.size() == gamut::kDiamond7.size());
    auto corrections = scaleCorrectionsFor(tuning, Scale::DIAMOND7, 0);
    int present = 0;
    for (int step = 0; step < edo; step++) present += corrections.forNote(step) ? 1 : 0;
    CHECK(present == 13);
  }
}
