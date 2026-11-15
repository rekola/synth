#include "TestFramework.h"

#include "../src/playback/KeyboardNotes.h"
#include "../src/model/Note.h"
#include "../src/model/JustIntonation.h"

#include <algorithm>
#include <set>
#include <string>

namespace {
cellui::Input key(int id) {
  return cellui::Input(id, 0, 0, false, false, false, false, cellui::Input::Kind::PRESS);
}
}  // namespace

namespace {
// A key's note, in a song of the given key (53-EDO pitch class), at an octave.
KeyboardNote noteAt(int id, Scale scale, int song_key, int octave = 4, Tuning tuning = Tuning::EDO53) {
  auto note = keyToNote(id, tuning, scale, song_key, octave);
  CHECK(note.has_value());
  return note.value_or(KeyboardNote{});
}
constexpr int kA = 39; // A in 53-EDO, steps above C
} // namespace

TEST(keyboard_notes_31edo_utonal) {
  CHECK(keyToMidiNote(key('q'), Tuning::EDO31, Scale::UTONAL) == "C");
  CHECK(keyToMidiNote(key('w'), Tuning::EDO31, Scale::UTONAL) == "D♭↓");
  CHECK(keyToMidiNote(key('s'), Tuning::EDO31, Scale::UTONAL) == "D↓");
  CHECK(keyToMidiNote(key('e'), Tuning::EDO31, Scale::UTONAL) == "E𝄫");
}

// All 21 notes of otonal and utonal are on keys, and every key plays a note of the
// scale, in the spelling the scale uses (the layouts themselves are tested with the diamond scales).
TEST(keyboard_notes_otonal_and_utonal_have_a_key_for_each_note) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    auto names = scaleDegreeNames(scale);
    std::set<std::string> played;
    for (int id = 32; id < 127; id++) {
      std::string name(keyToMidiNote(key(id), Tuning::EDO31, scale));
      if (name.empty()) continue;
      CHECK(std::find(names.begin(), names.end(), name) != names.end());
      played.insert(name);
    }
    CHECK(played.size() == 21);
    CHECK(played == std::set<std::string>(names.begin(), names.end()));
  }
  CHECK(keyToMidiNote(key('7'), Tuning::EDO53, Scale::OTONAL) == "F♯↑");
  CHECK(keyToMidiNote(key('6'), Tuning::EDO53, Scale::UTONAL) == "F♯↑");
  // Every note is a different key of 53-EDO, and the keys are the scale's.
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    std::set<int> keys;
    for (int id = 32; id < 127; id++) {
      std::string name(keyToMidiNote(key(id), Tuning::EDO53, scale));
      if (name.empty()) continue;
      keys.insert(Note::stringToKey(Tuning::EDO53, name) - Note::stringToKey(Tuning::EDO53, "C"));
    }
    CHECK(keys.size() == 21);
  }
}

TEST(keyboard_notes_scale_with_the_edo_per_octave) {
  CHECK(noteAt('q', Scale::OTONAL, 0, 5, Tuning::EDO31).value - noteAt('q', Scale::OTONAL, 0, 4, Tuning::EDO31).value == 31);
  CHECK(noteAt('q', Scale::OTONAL, 0, 5, Tuning::EDO53).value - noteAt('q', Scale::OTONAL, 0, 4, Tuning::EDO53).value == 53);
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

// The Pythagorean scales run in ascending order on q..p; Phrygian tonos has ten
// degrees, so B↑ is on p, and the seven-degree modes continue with the tonic and
// second an octave up.
TEST(keyboard_pythagorean_scales_are_on_the_q_row) {
  auto names = scaleDegreeNames(Scale::PHRYGIAN_TONOS);
  CHECK(names.size() == 10);
  std::string row = "qwertyuiop";
  for (size_t i = 0; i < row.size() && i < names.size(); i++) {
    CHECK(std::string(keyToMidiNote(key(row[i]), Tuning::EDO53, Scale::PHRYGIAN_TONOS)) == std::string(names[i]));
  }
  CHECK(std::string(keyToMidiNote(key('p'), Tuning::EDO53, Scale::PHRYGIAN_TONOS)) == "B↑");
  CHECK(std::string(keyToMidiNote(key('z'), Tuning::EDO53, Scale::PHRYGIAN_TONOS)).empty());
  CHECK(std::string(keyToMidiNote(key('u'), Tuning::EDO53, Scale::KITMUM)) == "B♭↓");
  CHECK(std::string(keyToMidiNote(key('i'), Tuning::EDO53, Scale::KITMUM)) == "C'");
  CHECK(std::string(keyToMidiNote(key('o'), Tuning::EDO53, Scale::KITMUM)) == "D'");
  CHECK(std::string(keyToMidiNote(key('p'), Tuning::EDO53, Scale::KITMUM)).empty());
}

// Rāst's seven degrees, with the tonic and second an octave up, are on q..o.
TEST(keyboard_rast_is_on_the_q_row) {
  const char * expected[] = {"C", "D", "E𝄳", "F", "G", "A", "B𝄳", "C'", "D'"};
  std::string row = "qwertyuio";
  for (size_t i = 0; i < row.size(); i++) {
    CHECK(std::string(keyToMidiNote(key(row[i]), Tuning::EDO24, Scale::RAST)) == expected[i]);
  }
  CHECK(std::string(keyToMidiNote(key('z'), Tuning::EDO24, Scale::RAST)).empty());
  CHECK(std::string(keyToMidiNote(key('p'), Tuning::EDO24, Scale::RAST)).empty());
}

// In the key of C every key gives the note its name says in 53-EDO, and the
// correction the tuner gave it. In the other tunings a key is the nearest step of
// its ratio; that is the name's step except for keys whose name is a step off (the
// name's arrow is ignored in those tunings).
TEST(keyboard_notes_ratios_in_c_are_the_notes_of_the_layout) {
  int off_name = 0;
  for (auto tuning : {Tuning::EDO12, Tuning::EDO19, Tuning::EDO31, Tuning::EDO53}) {
    for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
      auto corrections = just_intonation::scaleCorrectionsFor(tuning, scale, 0);
      for (int id = 32; id < 127; id++) {
        auto name = keyToMidiNote(key(id), tuning, scale);
        auto note = keyToNote(id, tuning, scale, 0, 4);
        CHECK(note.has_value() == !name.empty());
        if (!note) continue;
        int named = Note(name, tuning, 0x28, 0, 4).getValue();
        if (tuning == Tuning::EDO53) {
          CHECK(note->value == named);
          // Where the tuner holds this key for the same note, its correction is the key's.
          auto held = corrections.forNote(note->value);
          if (held) CHECK_NEAR(*held, note->correction_cents, 1e-9);
        } else if (note->value != named) {
          off_name++;
          CHECK(std::abs(note->value - named) == 1);
        }
      }
    }
  }
  CHECK(off_name > 0);
}

// Q is the tonic whatever the key; each key lies its ratio above it.
TEST(keyboard_notes_give_ratios_above_the_tonic_in_any_key) {
  const int c = 5 * 53;
  for (int song_key : {0, kA, 9, 48}) {
    for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
      auto tonic = noteAt('q', scale, song_key);
      CHECK(tonic.value == c + song_key);
      CHECK_NEAR(tonic.correction_cents, 0.0, 1e-9);
      for (int id = 32; id < 127; id++) {
        auto in_c = keyToNote(id, Tuning::EDO53, scale, 0, 4);
        if (!in_c) continue;
        auto here = noteAt(id, scale, song_key);
        CHECK(here.value - tonic.value == in_c->value - c);
        CHECK_NEAR(here.correction_cents, in_c->correction_cents, 1e-9);
        CHECK(here.gamut_index == in_c->gamut_index);
      }
    }
  }
}

TEST(keyboard_notes_in_a_otonal_spell_the_ratios_above_a) {
  // 5/4 above A is C♯; 3/2 is E.
  CHECK(Note::keyToString(Tuning::EDO53, noteAt('y', Scale::OTONAL, kA).value % 53) == "C♯");
  CHECK(Note::keyToString(Tuning::EDO53, noteAt('j', Scale::OTONAL, kA).value % 53) == "E");
  CHECK(Note::keyToString(Tuning::EDO53, noteAt('q', Scale::OTONAL, kA).value % 53) == "A");
  // The correction is the one of the ratio: 5/4 is 13.7 cents below its step.
  const double five_four = Ratio(5, 4).cents() - 17 * 1200.0 / 53;
  CHECK_NEAR(noteAt('y', Scale::OTONAL, kA).correction_cents, five_four, 1e-9);
  // The same key in utonal: 6/5 above A is C.
  CHECK(Note::keyToString(Tuning::EDO53, noteAt('4', Scale::UTONAL, kA).value % 53) == "C");
}

// A key sounds the exact ratio: the tuner's correction at the key's note, which
// is what a played note is given, equals the key's own.
TEST(keyboard_notes_sound_the_exact_ratio_in_any_key) {
  for (int song_key : {0, kA, 9, 48}) {
    for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
      auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO53, scale, song_key);
      for (int id = 32; id < 127; id++) {
        auto note = keyToNote(id, Tuning::EDO53, scale, song_key, 4);
        if (!note) continue;
        auto held = corrections.forNote(note->value);
        CHECK(held.has_value());
        if (held) CHECK_NEAR(*held, note->correction_cents, 1e-9);
      }
    }
  }
}

// The octave is counted from the tonic: Q is the tonic in the current octave, the
// keys above it rise from there.
TEST(keyboard_notes_count_the_octave_from_the_tonic) {
  for (int octave : {3, 4, 6}) {
    auto q = noteAt('q', Scale::OTONAL, kA, octave);
    CHECK(q.value == (octave + 1) * 53 + kA);
    CHECK(noteAt('u', Scale::OTONAL, kA, octave).value - q.value == 22);
    CHECK(noteAt('i', Scale::OTONAL, kA, octave).value - q.value == 41);
    CHECK(noteAt('p', Scale::OTONAL, kA, octave).value - q.value == 48);
  }
}

// A layout that is fixed notes gives no ratio.
TEST(keyboard_notes_other_layouts_give_no_ratio) {
  CHECK(!keyToNote('z', Tuning::EDO53, Scale::MAJOR, kA, 4).has_value());
  CHECK(!keyToNote('q', Tuning::EDO53, Scale::ISARTUM, kA, 4).has_value());
  CHECK(!keyToNote('x', Tuning::PERCUSSION, Scale::NONE, 0, 4).has_value());
}
