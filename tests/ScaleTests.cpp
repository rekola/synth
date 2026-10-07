#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/Note.h"
#include "../src/state/MemoryParameterSource.h"

using namespace std;

// Scale::NONE (the default - no scale ever set) falls back to the plain
// chromatic scale, one degree per step ascending from the tonic, capped
// at 8 (the step grid's own row count).
TEST(scale_none_falls_back_to_chromatic) {
  Song song(Tuning::EDO12, -1); // no key set either - tonic defaults to C (pitch class 0)
  CHECK(song.getScale() == Scale::NONE);
  auto degrees = song.getScaleDegreesWindow(0, 8);
  CHECK(degrees.size() == 8);
  for (int i = 0; i < 8; i++) CHECK(degrees[static_cast<size_t>(i)] == i);
}

// Major in 12-EDO, no key set (tonic = C = pitch class 0): the ordinary
// C D E F G A B major scale, as plain semitone offsets from C, plus an
// 8th and final row repeating the tonic (C) one octave up - filling the
// step grid's full 8 rows even though the scale itself only has 7
// distinct degrees.
TEST(scale_major_12edo_no_key) {
  Song song(Tuning::EDO12, -1);
  song.setScale(Scale::MAJOR);
  auto degrees = song.getScaleDegreesWindow(0, 8);
  vector<int> expected = {0, 2, 4, 5, 7, 9, 11, 12};
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// Minor in 12-EDO, no key set: C D E♭ F G A♭ B♭ natural minor, plus the
// same octave-up tonic repeat as the 8th row.
TEST(scale_minor_12edo_no_key) {
  Song song(Tuning::EDO12, -1);
  song.setScale(Scale::MINOR);
  auto degrees = song.getScaleDegreesWindow(0, 8);
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
  Song song(Tuning::EDO12, static_cast<short>(Note::stringToKey(Tuning::EDO12, "D4")));
  song.setScale(Scale::MAJOR);
  auto degrees = song.getScaleDegreesWindow(0, 8);
  // D major: D E F# G A B C# D
  vector<int> expected = {2, 4, 6, 7, 9, 11, 13, 14};
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

// The two special microtonal scales: C D♯ E F G A A♯ (OTONAL) and
// C E𝄫 E♭ F G A♭ B𝄫 (UTONAL), resolved in 31-EDO where D♯/A♯ and E𝄫/E♭
// are genuinely distinct pitches (12-EDO can't express them). Values are
// cross-checked against docs/31edo_note_numbers.txt's own published table
// (steps relative to its C-4 origin); the 8th row is the tonic 31 steps up
// (one 31-EDO octave).
TEST(scale_otonal_31edo) {
  Song song(Tuning::EDO31, -1);
  song.setScale(Scale::OTONAL);
  auto degrees = song.getScaleDegreesWindow(0, 8);
  vector<int> expected = {0, 7, 10, 13, 18, 23, 25, 31};
  CHECK(degrees.size() == expected.size());
  for (size_t i = 0; i < expected.size(); i++) CHECK(degrees[i] == expected[i]);
}

TEST(scale_utonal_31edo) {
  Song song(Tuning::EDO31, -1);
  song.setScale(Scale::UTONAL);
  auto degrees = song.getScaleDegreesWindow(0, 8);
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
  Song song(Tuning::EDO12, -1);
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
  Song song(Tuning::EDO12, -1);
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
  Song song(Tuning::EDO12, -1);
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
  Song song(Tuning::EDO12, -1);
  song.storeParameters(output);
  CHECK(output.get<string>("scale").empty());
}

// The Launchpad's in-key keyboard asks for major when no scale is chosen,
// and a fourth up a row (three degrees) lands on F above C.
TEST(scale_none_reads_as_major_for_the_keyboard) {
  Song song(Tuning::EDO12, -1);
  auto degrees = song.getScaleDegreesWindow(0, 29, true);
  CHECK(degrees.size() == 29);
  CHECK(degrees[1] == 2);  // D
  CHECK(degrees[3] == 5);  // F: one row up from C
  CHECK(degrees[7] == 12); // the octave
  CHECK(degrees[28] == 12 * 4); // four octaves up, still the tonic
  song.setScale(Scale::MINOR);
  CHECK(song.getScaleDegreesWindow(0, 3, true)[2] == 3); // a named scale is untouched
}
