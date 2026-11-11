#include "TestFramework.h"

#include "../src/model/Gamut.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/Scale.h"

#include <cmath>
#include <optional>
#include <set>
#include <vector>

using namespace just_intonation;

using namespace ratio_literals;

TEST(a_note_carries_a_tuning_correction_as_a_signed_hex_fx) {
  Note note(60, 100);
  CHECK(!note.hasFx());
  CHECK(note.getFx() == "...");
  CHECK(!note.hasTuningCorrection());
  CHECK(note.getTuningCorrectionCents() == 0);

  note.setTuningCorrection(-14);
  CHECK(note.getFx() == "-0E");
  CHECK(note.hasTuningCorrection());
  CHECK(note.getTuningCorrectionCents() == -14);

  note.setTuningCorrection(0);
  CHECK(note.getFx() == "+00"); // tuned, nothing to correct
  CHECK(note.hasTuningCorrection());

  note.setTuningCorrection(1000);
  CHECK(note.getTuningCorrectionCents() == Note::kMaxTuningCorrectionCents);
  note.setTuningCorrection(-1000);
  CHECK(note.getTuningCorrectionCents() == -Note::kMaxTuningCorrectionCents);

  // Typed by hand, a letter is accepted as a hex digit; half a value is not a correction yet.
  CHECK(note.setFx("+1a"));
  CHECK(note.getTuningCorrectionCents() == 26);
  CHECK(note.setFx("+1."));
  CHECK(!note.hasTuningCorrection());
  CHECK(!note.setFx("+1G"));

  note.clearFx();
  CHECK(!note.hasFx());
  CHECK(note.getFx() == "...");
}

namespace {

// Steps above C of a scale's degrees, as the tuner places them: each gamut
// note on its nearest step.
std::vector<int> degreeSteps(Tuning tuning, Scale scale) {
  int edo = edoStepsFor(tuning);
  std::vector<int> steps;
  auto just = justScale(scale);
  if (!just) return steps;
  for (auto i : scaleGamutIndices(scale)) steps.push_back(((just->gamut[i].ratio.steps(edo) % edo) + edo) % edo);
  return steps;
}

} // namespace

TEST(scale_corrections_give_each_degree_its_exact_ratio_in_every_tuning) {
  struct Row {
    Tuning tuning;
    std::vector<int> otonal, utonal;
  };
  const std::vector<Row> rows = {
      // Where two degrees of a scale share a step (12 and 19-EDO), the closer wins.
      {Tuning::EDO12, {0, 0, 19, 4, -33, -14, -14, -14, -2, -17, 2, 2, 49, -16, 18, 18, -12, 0}, {0, 0, 12, -18, -18, 16, -49, -2, -2, 17, 2, 14, 14, 14, 33, -4, -19, 0}},
      {Tuning::EDO19, {0, -27, -7, 14, 14, 7, 7, -7, -7, 14, 22, 7, 28, 0, 21, 7, 15, 28}, {0, -28, -15, -7, -21, 0, -28, -7, -22, -14, 7, 7, -7, -7, -14, -14, 7, 27}},
      {Tuning::EDO31, {0, -3, 3, 10, -4, 3, -1, 9, -5, 2, -5, 5, -2, -6, 1, 11, 4, 4}, {0, -4, -4, -11, -1, 6, 2, -5, 5, -2, 5, -9, 1, -3, 4, -10, -3, 3}},
      {Tuning::EDO53, {0, -10, 6, 0, -5, -11, 1, 5, 0, -6, -3, 0, -11, 1, -5, -1, 1, 10}, {0, -10, -1, 1, 5, -1, 11, 0, 3, 6, 0, -5, -1, 11, 5, 0, -6, 10}},
  };
  for (auto & row : rows) {
    for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
      auto & expected = scale == Scale::OTONAL ? row.otonal : row.utonal;
      auto corrections = scaleCorrectionsFor(row.tuning, scale, 0);
      auto steps = degreeSteps(row.tuning, scale);
      for (size_t i = 0; i < steps.size(); i++) {
        auto cents = corrections.forNote(steps[i]);
        CHECK(cents && *cents == expected[i]);
      }
    }
  }
}

TEST(otonal_and_utonal_are_tuned_together) {
  // The keys of the whole gamut are tuned in either scale: 29 in 53-EDO, 25 in 31.
  for (auto [tuning, expected] : {std::pair{Tuning::EDO31, 25}, std::pair{Tuning::EDO53, 29}}) {
    for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
      auto corrections = scaleCorrectionsFor(tuning, scale, 0);
      int present = 0;
      for (int step = 0; step < corrections.edo; step++) present += corrections.forNote(step) ? 1 : 0;
      CHECK(present == expected);
    }
  }
  // An otonal song's utonal notes carry the utonal ratios.
  auto otonal = scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, 0);
  CHECK(otonal.forNote(6) == std::optional<int>(-1)); // 8/7
  CHECK(otonal.forNote(8) == std::optional<int>(6));  // 6/5
  CHECK(otonal.forNote(21) == std::optional<int>(1)); // 8/5
  CHECK(otonal.forNote(24) == std::optional<int>(4)); // 12/7
}

namespace {

int stepOf(Tuning tuning, std::string_view spelling) {
  int edo = edoStepsFor(tuning);
  return (((Note::stringToKey(tuning, spelling) - Note::stringToKey(tuning, "C")) % edo) + edo) % edo;
}

int keyOf(Ratio ratio, int edo) { return ((ratio.steps(edo) % edo) + edo) % edo; }

std::optional<GamutNote> noteOf(std::span<const GamutNote> table, Ratio ratio) {
  for (auto & note : table)
    if (note.ratio == ratio) return note;
  return std::nullopt;
}

} // namespace

TEST(the_duodene_has_one_note_per_12_edo_key_and_none_share_a_key_elsewhere) {
  CHECK(gamut::kDuodene.size() == 12);
  for (auto tuning : {Tuning::EDO12, Tuning::EDO19, Tuning::EDO31, Tuning::EDO53}) {
    int edo = edoStepsFor(tuning);
    std::set<int> keys, spelled;
    for (auto & note : gamut::kDuodene) {
      keys.insert(keyOf(note.ratio, edo));
      spelled.insert(stepOf(tuning, note.spelling));
    }
    CHECK(keys.size() == 12);
    CHECK(spelled.size() == 12);
  }
  // The spellings mean the ratios in 31 and 53-EDO.
  for (auto tuning : {Tuning::EDO31, Tuning::EDO53}) {
    for (auto & note : gamut::kDuodene) CHECK(stepOf(tuning, note.spelling) == keyOf(note.ratio, edoStepsFor(tuning)));
  }
}

TEST(major_and_minor_are_selections_from_the_duodene) {
  CHECK(scaleGamut(Scale::MAJOR).data() == gamut::kDuodene.data());
  CHECK(scaleGamut(Scale::MINOR).data() == gamut::kDuodene.data());
  CHECK(scaleGamutIndices(Scale::MAJOR).size() == 7);
  CHECK(scaleGamutIndices(Scale::MINOR).size() == 7);
  for (auto scale : {Scale::MAJOR, Scale::MINOR}) {
    for (auto i : scaleGamutIndices(scale)) CHECK(i < gamut::kDuodene.size());
  }
  // Their degree names are the duodene's spellings.
  for (auto scale : {Scale::MAJOR, Scale::MINOR}) {
    for (auto name : scaleDegreeNames(scale)) {
      bool found = false;
      for (auto & note : gamut::kDuodene) found = found || note.spelling == name;
      CHECK(found);
    }
  }
}

TEST(the_gamut_of_each_scale) {
  CHECK(scaleGamut(Scale::OTONAL).data() == gamut::kExtendedDiamond.data());
  CHECK(scaleGamut(Scale::UTONAL).data() == gamut::kExtendedDiamond.data());
  CHECK(scaleGamut(Scale::KITMUM).data() == gamut::kPythagorean.data());
  for (auto scale : {Scale::NONE, Scale::RAST, Scale::BAYATI, Scale::HIJAZ, Scale::NAHAWAND, Scale::KURD, Scale::AJAM, Scale::SABA, Scale::SIKAH}) {
    CHECK(scaleGamut(scale).empty());
  }
}

TEST(b_d_flat_d_and_b_flat_are_15_8_16_15_9_8_and_9_5_in_all_four_tunings) {
  for (auto tuning : {Tuning::EDO12, Tuning::EDO19, Tuning::EDO31, Tuning::EDO53}) {
    int edo = edoStepsFor(tuning);
    for (auto scale : {Scale::MAJOR, Scale::MINOR}) {
      auto corrections = scaleCorrectionsFor(tuning, scale, 0);
      for (auto ratio : {15 / 8_r, 16 / 15_r, 9 / 8_r, 9 / 5_r}) {
        auto cents = corrections.forNote(keyOf(ratio, edo));
        CHECK(cents && *cents == std::lround(ratio.cents() - keyOf(ratio, edo) * 1200.0 / edo));
      }
    }
  }
}

TEST(scale_corrections_follow_the_key_and_ignore_octaves) {
  int key = 31 * 4 + 13; // F
  auto corrections = scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, key);
  CHECK(corrections.forNote(key + 25) == std::optional<int>(1)); // 7/4 above F
  CHECK(corrections.forNote(key + 25 - 31 * 2) == std::optional<int>(1));
  CHECK(!corrections.forNote(key + 2)); // no gamut note has this key
  // No key reads as C.
  CHECK(scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, -1) == scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, 0));
}

TEST(a_scale_without_a_gamut_applies_no_correction) {
  for (auto scale : {Scale::NONE, Scale::RAST, Scale::HIJAZ, Scale::SIKAH}) {
    for (auto tuning : {Tuning::EDO12, Tuning::EDO24, Tuning::EDO31, Tuning::EDO53}) {
      auto corrections = scaleCorrectionsFor(tuning, scale, 0);
      CHECK(corrections.empty());
      for (int note = 0; note < 60; note++) CHECK(!corrections.forNote(note));
    }
  }
  CHECK(scaleCorrectionsFor(Tuning::PERCUSSION, Scale::MAJOR, 0).empty());
}

TEST(a_shared_key_goes_to_the_scales_note_else_the_closer) {
  // 31-EDO key 3: 16/15 (utonal) and 15/14 (otonal).
  CHECK(scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, 0).forNote(3) == std::optional<int>(3));
  CHECK(scaleCorrectionsFor(Tuning::EDO31, Scale::UTONAL, 0).forNote(3) == std::optional<int>(-4));
  // 12-EDO key 3: 7/6 (otonal) and 6/5 (utonal).
  CHECK(scaleCorrectionsFor(Tuning::EDO12, Scale::OTONAL, 0).forNote(3) == std::optional<int>(-33));
  CHECK(scaleCorrectionsFor(Tuning::EDO12, Scale::UTONAL, 0).forNote(3) == std::optional<int>(16));
  // A key of neither scale's note in a major song: the closer. 12-EDO key 1
  // holds 16/15 (+12) and 15/14 (+19); major has 16/15.
  CHECK(scaleCorrectionsFor(Tuning::EDO12, Scale::MAJOR, 0).forNote(1) == std::optional<int>(12));
  CHECK(scaleCorrectionsFor(Tuning::EDO12, Scale::MAJOR, 0).forNote(2) == std::optional<int>(4));
}

TEST(every_correction_is_a_gamut_ratios_cents_minus_its_steps) {
  for (auto scale : {Scale::MAJOR, Scale::MINOR, Scale::OTONAL, Scale::UTONAL}) {
    for (auto tuning : {Tuning::EDO12, Tuning::EDO19, Tuning::EDO31, Tuning::EDO53}) {
      int edo = edoStepsFor(tuning);
      auto corrections = scaleCorrectionsFor(tuning, scale, 0);
      for (int step = 0; step < edo; step++) {
        auto cents = corrections.forNote(step);
        if (!cents) continue;
        const int value = *cents;
        bool matches = false;
        for (auto & note : scaleGamut(scale)) {
          matches = matches || (keyOf(note.ratio, edo) == step && value == std::lround(note.ratio.cents() - step * 1200.0 / edo));
        }
        CHECK(matches);
      }
    }
  }
}

TEST(the_duodenes_fixed_comma_is_where_the_docs_say) {
  // C major's D-A is 40/27 and F-B♭ in minor 27/20, both a comma from pure.
  CHECK(std::fabs((5 / 3_r).cents() - (9 / 8_r).cents() - (40 / 27_r).cents()) < 1e-9);
  CHECK(std::fabs((9 / 5_r).cents() - (4 / 3_r).cents() - (27 / 20_r).cents()) < 1e-9);
  CHECK(noteOf(gamut::kDuodene, 9 / 5_r).has_value());
  CHECK(!noteOf(gamut::kDuodene, 16 / 9_r).has_value());
}
