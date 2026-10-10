#include "TestFramework.h"

#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"

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
