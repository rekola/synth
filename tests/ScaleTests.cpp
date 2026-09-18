#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/Note.h"
#include "../src/state/MemoryParameterSource.h"

using namespace std;

// Scale::NONE (the default - no scale ever set) falls back to the plain
// chromatic scale, one degree per step ascending from the tonic, capped
// at 8 (the step grid's own row count).
TEST(scale_none_falls_back_to_chromatic) {
  Song song(Tuning::TET12, -1); // no key set either - tonic defaults to C (pitch class 0)
  CHECK(song.getScale() == Scale::NONE);
  auto degrees = song.getScaleDegrees();
  CHECK(degrees.size() == 8);
  for (int i = 0; i < 8; i++) CHECK(degrees[static_cast<size_t>(i)] == i);
}

// Major in 12-EDO, no key set (tonic = C = pitch class 0): the ordinary
// C D E F G A B major scale, as plain semitone offsets from C, plus an
// 8th and final row repeating the tonic (C) one octave up - filling the
// step grid's full 8 rows even though the scale itself only has 7
// distinct degrees.
TEST(scale_major_12edo_no_key) {
  Song song(Tuning::TET12, -1);
  song.setScale(Scale::MAJOR);
  auto degrees = song.getScaleDegrees();
  vector<int> expected = {0, 2, 4, 5, 7, 9, 11, 12};
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// Minor in 12-EDO, no key set: C D E♭ F G A♭ B♭ natural minor, plus the
// same octave-up tonic repeat as the 8th row.
TEST(scale_minor_12edo_no_key) {
  Song song(Tuning::TET12, -1);
  song.setScale(Scale::MINOR);
  auto degrees = song.getScaleDegrees();
  vector<int> expected = {0, 2, 3, 5, 7, 8, 10, 12};
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// Major transposed to the song's own key (D, pitch class 2) - every
// degree shifts by the same amount the tonic itself did, ascending past
// the octave boundary rather than wrapping back below the tonic (the 7th
// degree, C#, genuinely falls a semitone above the octave point relative
// to D), and the 8th row repeats the tonic (D) one octave up.
TEST(scale_major_12edo_transposed_to_key) {
  Song song(Tuning::TET12, static_cast<short>(Note::stringToKey(Tuning::TET12, "D4")));
  song.setScale(Scale::MAJOR);
  auto degrees = song.getScaleDegrees();
  // D major: D E F# G A B C# D
  vector<int> expected = {2, 4, 6, 7, 9, 11, 13, 14};
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// The two special microtonal scales, exactly as specified: C D♯ E F G A
// A♯ (MICROTONAL_A) and C E𝄫 E♭ F G A♭ B𝄫 (MICROTONAL_B) - both resolved
// here in 12-EDO, where E𝄫 degrades to (sounds identical to) D but still
// resolves to a real, correct pitch class rather than asserting/crashing
// (Note::stringToKey()'s own double-flat support, added alongside this
// feature). Each again gains an 8th row repeating the tonic an octave up.
TEST(scale_microtonal_a_12edo) {
  Song song(Tuning::TET12, -1);
  song.setScale(Scale::MICROTONAL_A);
  auto degrees = song.getScaleDegrees();
  vector<int> expected = {0, 3, 4, 5, 7, 9, 10, 12}; // C D# E F G A A# C
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

TEST(scale_microtonal_b_12edo) {
  Song song(Tuning::TET12, -1);
  song.setScale(Scale::MICROTONAL_B);
  auto degrees = song.getScaleDegrees();
  vector<int> expected = {0, 2, 3, 5, 7, 8, 9, 12}; // C E𝄫(=D) E♭ F G A♭ B𝄫(=A) C
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// The same MICROTONAL_B scale in 31-EDO, where E𝄫 and E♭ are genuinely
// distinct pitches (not degenerate the way they are in 12-EDO) - the
// whole reason these are called "microtonal" scales in the first place.
// Values cross-checked directly against docs/31edo_note_numbers.txt's
// own published table (steps 0/6/8/13/18/21/24 relative to that table's
// own C-4 origin at step 155); the 8th row is the tonic 31 steps up (one
// 31-EDO octave).
TEST(scale_microtonal_b_31edo) {
  Song song(Tuning::TET31, -1);
  song.setScale(Scale::MICROTONAL_B);
  auto degrees = song.getScaleDegrees();
  vector<int> expected = {0, 6, 8, 13, 18, 21, 24, 31};
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// getScaleDegreesWindow() at a positive, mid-scale start_index - the step
// grid's own row-scroll (LaunchpadManager.cpp) windows into this directly
// rather than always starting from the tonic. Starting at index 3 (F) in
// C major should read F G A B C D E, continuing straight past the octave
// boundary at C (index 7) without resetting back to a lower value.
TEST(scale_degrees_window_positive_start) {
  Song song(Tuning::TET12, -1);
  song.setScale(Scale::MAJOR);
  auto degrees = song.getScaleDegreesWindow(3, 7);
  vector<int> expected = {5, 7, 9, 11, 12, 14, 16}; // F G A B C D E
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// getScaleDegreesWindow() at a negative start_index - scrolling below the
// tonic (row-scroll's own "move-row-down" direction) should descend into
// the octave below rather than clamping at 0 or wrapping around to the
// top of the scale.
TEST(scale_degrees_window_negative_start) {
  Song song(Tuning::TET12, -1);
  song.setScale(Scale::MAJOR);
  auto degrees = song.getScaleDegreesWindow(-3, 3);
  vector<int> expected = {-5, -3, -1}; // G A B, one octave below C4 (index -3/-2/-1 = the scale's own last 3 degrees an octave down)
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// Scale round-trips through Song's own XML save/load, same as key/tuning
// already do.
TEST(scale_xml_round_trip) {
  MemoryParameterSource output;
  Song song(Tuning::TET12, -1);
  song.setScale(Scale::MINOR);
  song.storeParameters(output);
  CHECK(output.get<string>("scale") == "minor");

  Song loaded;
  loaded.loadParameters(output);
  CHECK(loaded.getScale() == Scale::MINOR);
}

// Scale::NONE writes nothing at all (same "omit rather than write a
// no-op default" convention getKey()'s own "key" attribute already
// follows for an unset key) - an old song file with no <song scale="..."/>
// at all loads back as Scale::NONE, not some other default.
TEST(scale_none_omitted_from_xml) {
  MemoryParameterSource output;
  Song song(Tuning::TET12, -1);
  song.storeParameters(output);
  CHECK(output.get<string>("scale").empty());
}
