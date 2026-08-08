#include "TestFramework.h"

#include "../Command.h"
#include "../EventHandler.h"
#include "../InputEvent.h"

// PatternEditor::offerInput()'s ColumnType::EFFECT branch uses this to
// gate what can be typed into a command's first two (mnemonic) characters
// - see its own comment for why a notcurses special-key code (arrows,
// F-keys, Insert, PageUp, ...) must never reach here.
TEST(command_is_mnemonic_char_accepts_letters_digits_and_slash) {
  CHECK(Command::isMnemonicChar('Z'));
  CHECK(Command::isMnemonicChar('b'));
  CHECK(Command::isMnemonicChar('0'));
  CHECK(Command::isMnemonicChar('9'));
  CHECK(Command::isMnemonicChar('/'));
}

TEST(command_is_mnemonic_char_rejects_punctuation_and_key_codes) {
  CHECK(!Command::isMnemonicChar('-'));
  CHECK(!Command::isMnemonicChar(' '));
  CHECK(!Command::isMnemonicChar('.'));
  CHECK(!Command::isMnemonicChar(NCKEY_BACKSPACE));
  CHECK(!Command::isMnemonicChar(NCKEY_DEL));
  CHECK(!Command::isMnemonicChar(NCKEY_LEFT));
}
