#include "TestFramework.h"

#include "../src/playback/KeyboardNotes.h"
#include "../src/model/Note.h"

#include <set>
#include <string>

namespace {
cellui::Input key(int id) {
  return cellui::Input(id, 0, 0, false, false, false, false, cellui::Input::Kind::PRESS);
}
}  // namespace

TEST(keyboard_notes_31edo_utonal) {
  CHECK(keyToMidiNote(key('z'), Tuning::EDO31, Scale::UTONAL) == "C");
  CHECK(keyToMidiNote(key('s'), Tuning::EDO31, Scale::UTONAL) == "D♭↓");
  CHECK(keyToMidiNote(key('c'), Tuning::EDO31, Scale::UTONAL) == "E♭");
}

// Every diamond note has a key, and every key plays a diamond note (or the
// octave above one), in the same spelling the scale uses.
TEST(keyboard_notes_diamond7_has_a_key_for_each_note) {
  std::set<std::string> played;
  for (int id = 32; id < 127; id++) {
    std::string name(keyToMidiNote(key(id), Tuning::EDO31, Scale::DIAMOND7));
    if (name.empty()) continue;
    while (!name.empty() && name.back() == '\'') name.pop_back();
    bool in_diamond = false;
    for (auto & note : gamut::kDiamond7) in_diamond = in_diamond || note.spelling == name;
    CHECK(in_diamond);
    played.insert(name);
  }
  CHECK(played.size() == gamut::kDiamond7.size());
  CHECK(keyToMidiNote(key('f'), Tuning::EDO53, Scale::DIAMOND7) == "F♯↑");
  CHECK(keyToMidiNote(key('g'), Tuning::EDO53, Scale::DIAMOND7) == "G♭↓");
  // The tritones are the two free steps: 31-EDO 15 and 16, 53-EDO 26 and 27.
  auto steps = [](Tuning tuning, char k) {
    return Note::stringToKey(tuning, keyToMidiNote(key(k), tuning, Scale::DIAMOND7)) - Note::stringToKey(tuning, "C");
  };
  CHECK(steps(Tuning::EDO31, 'f') == 15 && steps(Tuning::EDO31, 'g') == 16);
  CHECK(steps(Tuning::EDO53, 'f') == 26 && steps(Tuning::EDO53, 'g') == 27);
}

TEST(keyboard_notes_scale_with_the_edo_per_octave) {
  Note edo_31_a(keyToMidiNote(key('z'), Tuning::EDO31, Scale::OTONAL), Tuning::EDO31);
  Note edo_31_b(keyToMidiNote(key(','), Tuning::EDO31, Scale::OTONAL), Tuning::EDO31);

  Note edo_53_a(keyToMidiNote(key('z'), Tuning::EDO53, Scale::OTONAL), Tuning::EDO53);
  Note edo_53_b(keyToMidiNote(key(','), Tuning::EDO53, Scale::OTONAL), Tuning::EDO53);

  CHECK(edo_31_b.getValue() - edo_31_a.getValue() == 31);
  CHECK(edo_53_b.getValue() - edo_53_a.getValue() == 53);
}

// The drum keys are the Launchpad's 4x4 rack: notes 36-51, left to right and
// then bottom to top, on the x, s, w and 2 rows.
TEST(keyboard_drums_are_the_launchpad_rack) {
  const char keys[] = "xcvbsdfgwert2345";
  for (int i = 0; i < 16; i++) {
    Note note(keyToMidiNote(key(keys[i]), Tuning::PERCUSSION, Scale::NONE), Tuning::PERCUSSION);
    CHECK(note.getValue() == 36 + i);
  }
  CHECK(keyToMidiNote(key('a'), Tuning::PERCUSSION, Scale::NONE) == ""); // note-off
  CHECK(keyToMidiNote(key('z'), Tuning::PERCUSSION, Scale::NONE) == "");
  CHECK(keyToMidiNote(key('1'), Tuning::PERCUSSION, Scale::NONE) == "");
}

TEST(keyboard_notes_unmapped_key_is_empty) {
  CHECK(keyToMidiNote(key(cellui::Key::Enter), Tuning::EDO31, Scale::UTONAL) == "");
  CHECK(keyToMidiNote(key('a'), Tuning::EDO31, Scale::OTONAL) == "");
  CHECK(keyToMidiNote(key('9'), Tuning::EDO31, Scale::UTONAL) == "");
  CHECK(keyToMidiNote(key('!'), Tuning::EDO12, Scale::NONE) == "");
  CHECK(keyToMidiNote(key('+'), Tuning::PERCUSSION, Scale::NONE) == "");
}
