#include "TestFramework.h"

#include "../src/Tuner.h"
#include "../src/Note.h"

#include <cmath>

TEST(tuner_12edo_a4_is_440hz) {
  Note a4(69); // MIDI note number for A4 in 12edo
  auto freq = Tuner::getFrequency(Tuning::TET12, a4);
  CHECK_NEAR(freq, 440.0f, 0.01f);
}

TEST(tuner_12edo_octave_doubles_frequency) {
  Note base(60);
  Note octave_up(72);
  auto f0 = Tuner::getFrequency(Tuning::TET12, base);
  auto f1 = Tuner::getFrequency(Tuning::TET12, octave_up);
  CHECK_NEAR(f1 / f0, 2.0f, 0.001f);
}

TEST(tuner_note_off_and_undefined_have_no_frequency) {
  Note off(60, 0); // velocity 0 => note-off
  CHECK(off.isOff());
  CHECK_NEAR(Tuner::getFrequency(Tuning::TET12, off), 0.0f, 1e-6f);

  Note undefined;
  CHECK(!undefined.isDefined());
}

TEST(tuner_31edo_octave_doubles_frequency) {
  Note base(100);
  Note octave_up(131); // 31edo: one octave is 31 steps
  auto f0 = Tuner::getFrequency(Tuning::TET31, base);
  auto f1 = Tuner::getFrequency(Tuning::TET31, octave_up);
  CHECK_NEAR(f1 / f0, 2.0f, 0.001f);
}
