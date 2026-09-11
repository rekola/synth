#include "TestFramework.h"

#include "../src/model/Command.h"
#include "../src/playback/EventHandler.h"
#include "../src/playback/InputEvent.h"

// PatternEditor::offerInput()'s ColumnType::EFFECT branch relies on
// updateData() to validate a raw InputEvent::getId() codepoint itself
// (see its own comment for why a notcurses special-key code - arrows,
// F-keys, Insert, PageUp, ... - must never reach values_ untested), rather
// than pre-classifying input with a separate function.
TEST(command_update_data_accepts_mnemonic_column_charset) {
  Command c;
  CHECK(c.updateData(0, 'Z'));
  CHECK(c.updateData(1, 'b')); // lowercase accepted, normalized on storage
  CHECK(c.updateData(0, '9'));
  CHECK(c.updateData(1, '-'));
}

TEST(command_update_data_rejects_slash_and_punctuation_on_mnemonic_columns) {
  Command c;
  CHECK(!c.updateData(0, '/'));
  CHECK(!c.updateData(0, ' '));
  CHECK(!c.updateData(0, '.'));
  CHECK(!c.updateData(1, NCKEY_BACKSPACE));
  CHECK(!c.updateData(0, NCKEY_DEL));
  CHECK(!c.updateData(1, NCKEY_LEFT));
}

// Column 2/3 (the hex argument) is narrower than the mnemonic columns -
// [A-Fa-f0-9-] only, no G-Z/g-z.
TEST(command_update_data_accepts_hex_charset_on_argument_columns) {
  Command c;
  CHECK(c.updateData(2, 'a'));
  CHECK(c.updateData(3, 'F'));
  CHECK(c.updateData(2, '0'));
  CHECK(c.updateData(3, '-'));
}

TEST(command_update_data_rejects_non_hex_letters_on_argument_columns) {
  Command c;
  CHECK(!c.updateData(2, 'g'));
  CHECK(!c.updateData(3, 'Z'));
  CHECK(!c.updateData(2, '/'));
}

TEST(command_update_data_rejects_out_of_range_index) {
  Command c;
  CHECK(!c.updateData(-1, 'A'));
  CHECK(!c.updateData(4, 'A'));
}

// Column 0 (docs/commands.md's own device index) is either 'Z' (a global
// command) or a digit (how far up the track's own ancestor chain the
// command targets - only '0'/'-' does anything at playback today, but
// any digit is syntactically valid, matching docs/commands.md's own "not
// implemented beyond -/0 yet" note) - unlike column 1, no other letter
// means anything there, so one shouldn't silently parse as a "valid"
// command either.
TEST(command_update_data_column_zero_accepts_only_z_and_digits) {
  Command c;
  CHECK(c.updateData(0, 'Z'));
  CHECK(c.updateData(0, '0'));
  CHECK(c.updateData(0, '9')); // an unimplemented ancestor level - still syntactically valid
  CHECK(c.updateData(0, '-')); // normalizes to '0', see below
  CHECK(!c.updateData(0, 'A'));
  CHECK(!c.updateData(0, 'L')); // a real column-1 mnemonic letter, but meaningless at column 0
  CHECK(c.updateData(0, 'z')); // lowercase 'z' normalizes to 'Z' before this check, so it IS accepted
}

// setData() (the fallible sibling), not the asserting string_view
// constructor - 'L' isn't 'Z' or a digit, so this is deliberately
// malformed input.
TEST(command_set_data_rejects_a_command_with_a_meaningless_device_index) {
  Command c;
  CHECK(!c.setData("L400"));
}

// Column 0 is this engine's own leaf-track chain-position digit - always
// '0' today (docs/commands.md's own intro paragraph) - so '-' typed there
// is accepted as a synonym for '0' rather than left meaning "undefined",
// matching Renoise's own convention for the same digit. Every other
// column still stores a literal '-' when typed (Command's own "not set"
// placeholder, unaffected).
TEST(command_update_data_normalizes_a_dash_in_column_zero_to_zero) {
  Command c;
  CHECK(c.updateData(0, '-'));
  CHECK(c.updateData(1, 'L'));
  CHECK(c.updateData(2, '0'));
  CHECK(c.updateData(3, '0'));
  CHECK(to_string(c) == "0L00");
}

TEST(command_set_data_normalizes_a_leading_dash_to_zero) {
  Command c("-L00");
  CHECK(to_string(c) == "0L00");
  CHECK(c.isVolumeSet());
}

TEST(command_update_data_normalizes_lowercase_to_uppercase_on_storage) {
  Command c;
  CHECK(c.updateData(0, 'z'));
  CHECK(c.updateData(1, 'b'));
  CHECK(c.updateData(2, 'a'));
  CHECK(c.updateData(3, 'f'));
  CHECK(to_string(c) == "ZBAF");
}

// setData()/the Command(string_view) constructor route every character
// through updateData(), so a Command loaded from a file is held to
// exactly the same rules as one typed through the UI.
TEST(command_set_data_accepts_well_formed_input) {
  Command c;
  CHECK(c.setData("ZB02"));
  CHECK(to_string(c) == "ZB02");
}

TEST(command_set_data_rejects_malformed_input_and_leaves_command_unchanged) {
  Command c;
  CHECK(c.setData("ZB02"));
  CHECK(!c.setData("Z/02")); // '/' invalid on a mnemonic column
  CHECK(to_string(c) == "ZB02"); // unchanged, not partially overwritten
  CHECK(!c.setData("ZBGA")); // 'G' invalid on an argument column
  CHECK(to_string(c) == "ZB02");
  CHECK(!c.setData("ZB0")); // wrong length
  CHECK(to_string(c) == "ZB02");
}
