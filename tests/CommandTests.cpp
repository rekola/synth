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
