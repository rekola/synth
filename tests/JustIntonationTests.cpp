#include "TestFramework.h"

#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"

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
