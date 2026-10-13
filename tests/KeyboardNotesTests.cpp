#include "TestFramework.h"

#include "../src/playback/KeyboardNotes.h"

namespace {
cellui::Input key(int id) {
  return cellui::Input(id, 0, 0, false, false, false, false, cellui::Input::Kind::PRESS);
}
}  // namespace

TEST(keyboard_notes_12edo_middle_octave) {
  CHECK(keyToMidiNote(key('q'), 4, Tuning::EDO12) == 60);
  CHECK(keyToMidiNote(key('z'), 4, Tuning::EDO12) == 48);
  CHECK(keyToMidiNote(key('q'), 5, Tuning::EDO12) == 72);
}

TEST(keyboard_notes_scale_with_the_edo_per_octave) {
  CHECK(keyToMidiNote(key('z'), 5, Tuning::EDO31) - keyToMidiNote(key('z'), 4, Tuning::EDO31) == 31);
  CHECK(keyToMidiNote(key('z'), 5, Tuning::EDO53) - keyToMidiNote(key('z'), 4, Tuning::EDO53) == 53);
}

TEST(keyboard_notes_percussion_ignores_octave) {
  CHECK(keyToMidiNote(key('9'), 2, Tuning::PERCUSSION) == 35);
  CHECK(keyToMidiNote(key('9'), 7, Tuning::PERCUSSION) == 35);
}

TEST(keyboard_notes_unmapped_key_is_minus_one) {
  CHECK(keyToMidiNote(key(Key::Enter), 4, Tuning::EDO12) == -1);
  CHECK(keyToMidiNote(key('q'), 4, Tuning::PERCUSSION) == 37);
  CHECK(keyToMidiNote(key('!'), 4, Tuning::EDO12) == -1);
}
