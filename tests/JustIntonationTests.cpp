#include "TestFramework.h"

#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/Scale.h"

#include <optional>

#include <vector>

using namespace just_intonation;

TEST(just_intonation_12edo_major_scale_degrees) {
  CHECK(correctionCentsFor(12, 0) == 0);
  CHECK(correctionCentsFor(12, 2) == 4);    // 9/8
  CHECK(correctionCentsFor(12, 4) == -14);  // 5/4
  CHECK(correctionCentsFor(12, 5) == -2);   // 4/3
  CHECK(correctionCentsFor(12, 7) == 2);    // 3/2
  CHECK(correctionCentsFor(12, 9) == -16);  // 5/3
  CHECK(correctionCentsFor(12, 11) == -12); // 15/8
}

TEST(just_intonation_31edo_reaches_seven_and_eleven_without_a_limit_setting) {
  // C D# F is 6:7:8 above C.
  auto minor_third = intervalFor(31, 7);
  CHECK(minor_third.num == 7 && minor_third.den == 6);
  CHECK(correctionCentsFor(31, 7) == -4);
  CHECK(correctionCentsFor(31, 13) == -5); // 4/3

  // C C-double-sharp Eb G is 10:11:12:15.
  auto second = intervalFor(31, 4);
  CHECK(second.num == 11 && second.den == 10);
  CHECK(correctionCentsFor(31, 4) == 10);
  CHECK(correctionCentsFor(31, 8) == 6);  // 6/5
  CHECK(correctionCentsFor(31, 18) == 5); // 3/2
}

TEST(just_intonation_ignores_octaves_and_follows_the_key) {
  CHECK(correctionCentsFor(31, 18 + 31 * 3) == correctionCentsFor(31, 18));
  CHECK(correctionCentsFor(31, 18 - 31) == correctionCentsFor(31, 18));
  // A note is measured from the key's pitch class, whatever octave either is in.
  int key = 31 * 4 + 5;
  CHECK(correctionCentsForNote(Tuning::EDO31, key + 18, key) == 5);
  CHECK(correctionCentsForNote(Tuning::EDO31, key + 18 + 31 * 2, key) == 5);
  CHECK(correctionCentsForNote(Tuning::EDO31, key - 13, key) == correctionCentsFor(31, -13));
  CHECK(correctionCentsForNote(Tuning::EDO31, key, key) == 0);
}

TEST(just_intonation_53edo_is_nearly_just_already) {
  CHECK(std::abs(correctionCentsFor(53, 31)) <= 1); // 3/2
  CHECK(std::abs(correctionCentsFor(53, 17)) <= 2); // 5/4
}

TEST(just_intonation_never_picks_a_ratio_with_a_large_prime) {
  auto largest_prime = [](int n) {
    int largest = 1;
    for (int p = 2; p * p <= n; p++) {
      while (n % p == 0) {
        largest = p;
        n /= p;
      }
    }
    return n > 1 ? n : largest;
  };
  for (int edo : {12, 19, 31, 53}) {
    for (int step = 0; step < edo; step++) {
      auto interval = intervalFor(edo, step);
      CHECK(largest_prime(interval.num) <= kMaxPrime);
      CHECK(largest_prime(interval.den) <= kMaxPrime);
    }
  }
  CHECK(intervalFor(31, 1).num == 33); // not 31/30
}

TEST(just_intonation_has_a_ratio_for_every_step_and_none_for_percussion) {
  for (int edo : { 12, 19, 31, 53 }) {
    int found = 0;
    for (int step = 0; step < edo; step++) {
      auto interval = intervalFor(edo, step);
      if (interval.found) {
        found++;
        // Always within the cap, so the note stays the note it was.
        CHECK(std::abs(correctionCentsFor(edo, step)) <= static_cast<int>(kMaxErrorCents) + 1);
      } else {
        CHECK(correctionCentsFor(edo, step) == 0);
      }
    }
    CHECK(found >= edo - 2);
  }
  CHECK(correctionCentsForNote(Tuning::PERCUSSION, 36, 0) == 0);
}

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

TEST(a_chord_is_tuned_above_its_bass) {
  // Above a C bass the correction of each note is the same as measured from the key.
  CHECK(correctionCentsInChord(31, 7, 0, 0) == -4);  // D#: 7/6 above C
  CHECK(correctionCentsInChord(31, 13, 0, 0) == -5); // F: 4/3
  CHECK(correctionCentsInChord(31, 4, 0, 0) == 10);  // C double sharp: 11/10
  CHECK(correctionCentsInChord(31, 8, 0, 0) == 6);   // Eb: 6/5
  CHECK(correctionCentsInChord(31, 18, 0, 0) == 5);  // G: 3/2
  // The bass itself, and its octaves, are tuned from the key.
  CHECK(correctionCentsInChord(31, 5, 5, 0) == correctionCentsFor(31, 5));
  CHECK(correctionCentsInChord(31, 5 + 31, 5, 0) == correctionCentsFor(31, 5));

  // A ii chord, D F A, is pure against its D: F a 6/5 and A a 3/2 above it,
  // not F and A each measured from the key.
  auto pitch = [](int value, int correction) { return value * 1200.0 / 31 + correction; };
  double d = pitch(5, correctionCentsInChord(31, 5, 5, 0));
  double f = pitch(13, correctionCentsInChord(31, 13, 5, 0));
  double a = pitch(23, correctionCentsInChord(31, 23, 5, 0));
  CHECK_NEAR(f - d, 1200.0 * std::log2(6.0 / 5.0), 1.0);
  CHECK_NEAR(a - d, 1200.0 * std::log2(3.0 / 2.0), 1.0);
  CHECK(correctionCentsInChord(31, 13, 5, 0) != correctionCentsFor(31, 13));

  // Octaves of the key do not matter.
  CHECK(correctionCentsInChord(31, 13 + 31 * 4, 5 + 31 * 3, 31 * 2) == correctionCentsInChord(31, 13, 5, 0));
  CHECK(correctionCentsInChord(0, 13, 5, 0) == 0);

  // A song with no key (-1) is read as C, here as in the single-note case.
  CHECK(correctionCentsInChord(31, 13, 5, -1) == correctionCentsInChord(31, 13, 5, 0));
  CHECK(correctionCentsInChord(31, 5, 5, -1) == correctionCentsForNote(Tuning::EDO31, 5, -1));
}

TEST(chords_that_hold_the_tonic_tune_the_same_against_any_of_their_notes) {
  // Otonal/utonal chords in 31-EDO: tuned above any note of the chord, a note
  // gets the correction it has against the key, so nothing drifts.
  const std::vector<std::vector<int> > chords = {{0, 7, 13}, {13, 23, 31}, {0, 10, 18}, {18, 25, 31}};
  for (const auto & chord : chords) {
    for (int bass : chord) {
      for (int value : chord) {
        if (value < bass) continue;
        CHECK(correctionCentsInChord(31, value, bass, 0) == correctionCentsFor(31, value));
      }
    }
  }
}

namespace {

// Steps above C of a scale's degrees, from their names, as the tuner places them.
std::vector<int> degreeSteps(Tuning tuning, Scale scale) {
  int edo = edoStepsFor(tuning);
  int c = Note::stringToKey(tuning, "C");
  std::vector<int> steps;
  for (auto name : scaleDegreeNames(scale)) steps.push_back((((Note::stringToKey(tuning, name) - c) % edo) + edo) % edo);
  return steps;
}

} // namespace

TEST(scale_corrections_give_each_degree_its_exact_ratio_in_every_tuning) {
  struct Row {
    Tuning tuning;
    std::vector<int> otonal, utonal;
  };
  const std::vector<Row> rows = {
      // Where two degrees of a scale share a step (12-EDO), the first named wins.
      {Tuning::EDO12, {0, 39, 4, -33, -14, -2, 2, -16, -31, -31, -12}, {0, 12, -49, -49, 16, -2, 2, 14, 33, -4, -39}},
      {Tuning::EDO19, {0, 12, 14, 14, 7, -7, 7, 0, 21, -24, -24}, {0, -15, -15, -21, 0, -7, 7, -7, -14, -14, -12}},
      {Tuning::EDO31, {0, 22, 10, -4, -1, -5, 5, -6, 1, 4, 4}, {0, -4, -4, -1, 6, -5, 5, 1, 4, -10, -22}},
      {Tuning::EDO53, {0, 3, 0, -5, 1, 0, 0, 1, -5, 8, 1}, {0, -1, -8, 5, -1, 0, 0, -1, 5, 0, -3}},
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

TEST(scale_corrections_follow_the_key_and_ignore_octaves) {
  int key = 31 * 4 + 13; // F
  auto corrections = scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, key);
  CHECK(corrections.forNote(key + 25) == std::optional<int>(1)); // 7/4 above F
  CHECK(corrections.forNote(key + 25 - 31 * 2) == std::optional<int>(1));
  CHECK(!corrections.forNote(key + 1)); // not a note of either scale
  // No key reads as C.
  CHECK(scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, -1) == scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, 0));
}

TEST(otonal_and_utonal_are_tuned_together) {
  // The steps of both scales together: 31-EDO has one step for each pair of
  // degrees a syntonic comma apart, 53-EDO two.
  for (auto [tuning, expected] : {std::pair{Tuning::EDO31, 17}, std::pair{Tuning::EDO53, 19}}) {
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

TEST(where_the_two_scales_share_a_step_the_songs_own_scale_has_it) {
  // 12-EDO step 3 is the otonal 7/6 and the utonal 6/5.
  CHECK(scaleCorrectionsFor(Tuning::EDO12, Scale::OTONAL, 0).forNote(3) == std::optional<int>(-33));
  CHECK(scaleCorrectionsFor(Tuning::EDO12, Scale::UTONAL, 0).forNote(3) == std::optional<int>(16));
  // 19-EDO step 4 is the otonal 7/6 and the utonal 8/7.
  CHECK(scaleCorrectionsFor(Tuning::EDO19, Scale::OTONAL, 0).forNote(4) == std::optional<int>(14));
  CHECK(scaleCorrectionsFor(Tuning::EDO19, Scale::UTONAL, 0).forNote(4) == std::optional<int>(-21));
}

TEST(scales_without_ratios_have_no_scale_corrections) {
  for (auto scale : {Scale::NONE, Scale::MAJOR, Scale::MINOR}) CHECK(scaleCorrectionsFor(Tuning::EDO31, scale, 0).empty());
  CHECK(scaleCorrectionsFor(Tuning::PERCUSSION, Scale::OTONAL, 0).empty());
  // Without scale corrections a chord is tuned as before.
  ScaleCorrections none;
  CHECK(correctionCentsInChord(31, 23, 18, 0, none) == correctionCentsInChord(31, 23, 18, 0));
}

TEST(every_triad_of_the_two_scales_is_exact_in_every_voicing) {
  for (auto tuning : {Tuning::EDO31, Tuning::EDO53}) {
    int edo = edoStepsFor(tuning);
    auto scale = scaleCorrectionsFor(tuning, Scale::OTONAL, 0);
    std::vector<int> notes;
    for (int step = 0; step < edo; step++)
      if (scale.forNote(step)) notes.push_back(step);
    CHECK(notes.size() == (tuning == Tuning::EDO31 ? 17u : 19u));
    for (size_t a = 0; a < notes.size(); a++) {
      for (size_t b = a + 1; b < notes.size(); b++) {
        for (size_t c = b + 1; c < notes.size(); c++) {
          int triad[3] = {notes[a], notes[b], notes[c]};
          for (int bass : triad) {
            for (int note : triad) {
              int value = note < bass ? note + edo : note; // above the bass
              CHECK(std::optional<int>(correctionCentsInChord(edo, value, bass, 0, scale)) == scale.forNote(value));
            }
          }
        }
      }
    }
  }
  // The chord tuner alone gets an inverted C6 wrong: G C E A has A at 27/16.
  CHECK(correctionCentsInChord(31, 23 + 31, 18, 0) == 16);
  CHECK(correctionCentsInChord(31, 23 + 31, 18, 0, scaleCorrectionsFor(Tuning::EDO31, Scale::OTONAL, 0)) == -6);
}

TEST(a_note_outside_the_scale_is_tuned_against_the_exact_ratio_of_a_scale_bass) {
  // 12-EDO otonal: D-sharp is 7/6 (-33), F-sharp above it a 6/5.
  auto scale = scaleCorrectionsFor(Tuning::EDO12, Scale::OTONAL, 0);
  CHECK(!scale.forNote(6));
  CHECK(correctionCentsInChord(12, 6, 3, 0, scale) == -17);
  CHECK(correctionCentsInChord(12, 6, 3, 0) == 31); // the table's 6/5 for the bass instead
}
