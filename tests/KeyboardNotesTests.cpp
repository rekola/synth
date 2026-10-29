#include "TestFramework.h"

#include "../src/playback/KeyboardNotes.h"
#include "../src/model/Note.h"

namespace {
cellui::Input key(int id) {
  return cellui::Input(id, 0, 0, false, false, false, false, cellui::Input::Kind::PRESS);
}
}  // namespace

TEST(keyboard_notes_31edo_utonal) {
  CHECK(keyToMidiNote(key('z'), Tuning::EDO31, Scale::UTONAL) == "C");
  CHECK(keyToMidiNote(key('s'), Tuning::EDO31, Scale::UTONAL) == "D𝄭");
  CHECK(keyToMidiNote(key('c'), Tuning::EDO31, Scale::UTONAL) == "E♭");
}

TEST(keyboard_notes_scale_with_the_edo_per_octave) {
  Note edo_31_a(keyToMidiNote(key('z'), Tuning::EDO31, Scale::OTONAL), Tuning::EDO31);
  Note edo_31_b(keyToMidiNote(key(','), Tuning::EDO31, Scale::OTONAL), Tuning::EDO31);

  Note edo_53_a(keyToMidiNote(key('z'), Tuning::EDO53, Scale::OTONAL), Tuning::EDO53);
  Note edo_53_b(keyToMidiNote(key(','), Tuning::EDO53, Scale::OTONAL), Tuning::EDO53);

  CHECK(edo_31_b.getValue() - edo_31_a.getValue() == 31);
  CHECK(edo_53_b.getValue() - edo_53_a.getValue() == 53);
}

TEST(keyboard_notes_unmapped_key_is_empty) {
  CHECK(keyToMidiNote(key(cellui::Key::Enter), Tuning::EDO31, Scale::UTONAL) == "");
  CHECK(keyToMidiNote(key('a'), Tuning::EDO31, Scale::OTONAL) == "");
  CHECK(keyToMidiNote(key('9'), Tuning::EDO31, Scale::UTONAL) == "");
  CHECK(keyToMidiNote(key('!'), Tuning::EDO12, Scale::NONE) == "");
  CHECK(keyToMidiNote(key('+'), Tuning::PERCUSSION, Scale::NONE) == "");
}
