#include "TestFramework.h"

#include "../src/model/Command.h"
#include "../src/playback/EventHandler.h"
#include "../src/playback/InputEvent.h"

#include <cmath>

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

// Column 0 (docs/commands.md's own device index) is 'Z' (a global
// command), 'Y' (this engine's own reserved namespace) or a digit (how
// far up the track's own ancestor chain the command targets - only
// '0'/'-' does anything at playback today, but any digit is
// syntactically valid, matching docs/commands.md's own "not implemented
// beyond -/0 yet" note) - unlike column 1, no other letter means
// anything there, so one shouldn't silently parse as a "valid" command
// either.
TEST(command_update_data_column_zero_accepts_only_z_y_and_digits) {
  Command c;
  CHECK(c.updateData(0, 'Z'));
  CHECK(c.updateData(0, 'Y'));
  CHECK(c.updateData(0, '0'));
  CHECK(c.updateData(0, '9')); // an unimplemented ancestor level - still syntactically valid
  CHECK(c.updateData(0, '-')); // normalizes to '0', see below
  CHECK(!c.updateData(0, 'A'));
  CHECK(!c.updateData(0, 'L')); // a real column-1 mnemonic letter, but meaningless at column 0
  CHECK(c.updateData(0, 'z')); // lowercase 'z' normalizes to 'Z' before this check, so it IS accepted
  CHECK(c.updateData(0, 'y')); // lowercase 'y' normalizes to 'Y' before this check, so it IS accepted
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

// YMxy/YAxy/YBxy - the Y-namespace's own recorded fader-glide commands
// (docs/commands.md's own "Recorded fader-glide commands" section) -
// carry a target *and* a real-seconds duration, unlike 0Lxx/0Fxx/0Mxx's
// plain instant set.
TEST(glide_command_parses_mnemonic) {
  Command volume("YM00");
  CHECK(volume.isVolumeGlide());
  CHECK(!volume.isSendAGlide());
  CHECK(!volume.isSendBGlide());
  CHECK(!volume.isVolumeSet()); // a different namespace entirely, not the same command

  Command send_a("YA00");
  CHECK(send_a.isSendAGlide());
  CHECK(!send_a.isVolumeGlide());

  Command send_b("YB00");
  CHECK(send_b.isSendBGlide());
  CHECK(!send_b.isVolumeGlide());

  Command unrelated("YL10");
  CHECK(!unrelated.isVolumeGlide() && !unrelated.isSendAGlide() && !unrelated.isSendBGlide());
}

TEST(glide_command_decodes_the_full_range_at_nibble_resolution) {
  CHECK_NEAR(Command("YM00").getGlideTargetDb(), -80.0f, 1e-6f); // x=0 -> -80dB floor
  CHECK_NEAR(Command("YMF0").getGlideTargetDb(), 0.0f, 1e-6f); // x=F -> 0dB/unity
  CHECK_NEAR(Command("YM00").getGlideDurationSeconds(), 0.03f, 1e-6f); // y=0 -> fastest
  CHECK_NEAR(Command("YM0F").getGlideDurationSeconds(), 1.0f, 1e-6f); // y=F -> slowest
}

// Command::volumeGlide()/sendAGlide()/sendBGlide() - what
// LaunchpadManager::recordFaderAutomationIfArmed() actually builds from a
// live press's own resolveSendFaderTarget() output (target_db,
// duration_seconds - both already in this command's own native units, no
// linear-gain round trip needed unlike 0Lxx/0Fxx/0Mxx's own volumeSet()).
// A round trip through the 16-step nibble quantization can't be
// bit-exact, but should land close.
TEST(glide_command_factories_build_a_real_command_that_round_trips) {
  auto volume = Command::volumeGlide(-20.0f, 0.5f);
  CHECK(volume.isVolumeGlide());
  CHECK_NEAR(volume.getGlideTargetDb(), -20.0f, 3.0f); // nibble resolution is coarse
  CHECK_NEAR(volume.getGlideDurationSeconds(), 0.5f, 0.05f);

  auto send_a = Command::sendAGlide(-80.0f, 0.03f);
  CHECK(send_a.isSendAGlide());
  CHECK_NEAR(send_a.getGlideTargetDb(), -80.0f, 1e-6f);
  CHECK_NEAR(send_a.getGlideDurationSeconds(), 0.03f, 1e-6f);

  auto send_b = Command::sendBGlide(0.0f, 1.0f);
  CHECK(send_b.isSendBGlide());
  CHECK_NEAR(send_b.getGlideTargetDb(), 0.0f, 1e-6f);
  CHECK_NEAR(send_b.getGlideDurationSeconds(), 1.0f, 1e-6f);
}

// Values outside the representable range clamp rather than wrapping into
// an unrelated nibble (makeGlideSet()'s own comment) - both target dB and
// duration seconds.
TEST(glide_command_factories_clamp_out_of_range_values) {
  CHECK(to_string(Command::volumeGlide(-200.0f, 0.0f)) == "YM00"); // below floor and below min duration
  CHECK(to_string(Command::volumeGlide(20.0f, 100.0f)) == "YMFF"); // above unity and above max duration
}

// YLxx/YRxx - see docs/commands.md's own "Azimuth-slide commands"
// section and AzimuthSlideTests.cpp for the fuller pipeline test.
TEST(azimuth_slide_command_parses_left_and_right) {
  Command left("YL10"), right("YR10");
  CHECK(left.isAzimuthSlide());
  CHECK(right.isAzimuthSlide());
  CHECK(left.getAzimuthSlidePerTick() < 0.0f); // YLxx: left, negative
  CHECK(right.getAzimuthSlidePerTick() > 0.0f); // YRxx: right, positive
  CHECK_NEAR(std::fabs(left.getAzimuthSlidePerTick()), std::fabs(right.getAzimuthSlidePerTick()), 1e-6f); // same magnitude, opposite sign
}

// YDxy - azimuth's own equivalent of YMxy/YAxy/YBxy, full-circle target
// instead of 0Pxx's own half-circle one (see isAzimuthGlide()'s own
// comment for why it isn't called "YPxy").
TEST(azimuth_glide_command_parses_and_decodes_the_full_circle) {
  Command left("YD00");
  CHECK(left.isAzimuthGlide());
  CHECK(!left.isVolumeGlide() && !left.isSendAGlide() && !left.isSendBGlide());
  CHECK_NEAR(left.getAzimuthGlideTargetDegrees(), -180.0f, 1e-6f); // x=0 -> -180

  Command right("YDF0");
  CHECK_NEAR(right.getAzimuthGlideTargetDegrees(), 180.0f, 1e-6f); // x=F -> +180

  Command unrelated("YM00");
  CHECK(!unrelated.isAzimuthGlide());
}

// Command::azimuthGlide() - what LaunchpadManager::
// recordFaderAutomationIfArmed() builds from a live Pan press's own
// resolveAzimuthFaderTarget() output. Wraps rather than clamps (unlike
// the dB-based glide factories) - a target outside (-180,180] is just
// the same direction taken the long way round, not out of range.
TEST(azimuth_glide_factory_wraps_and_round_trips) {
  auto forward = Command::azimuthGlide(90.0f, 0.5f);
  CHECK(forward.isAzimuthGlide());
  CHECK_NEAR(forward.getAzimuthGlideTargetDegrees(), 90.0f, 15.0f); // nibble resolution is coarse (24 degrees/step)
  CHECK_NEAR(forward.getGlideDurationSeconds(), 0.5f, 0.05f);

  // 190 degrees is congruent to -170 - wraps into range rather than
  // clamping to the +180 ceiling.
  auto wrapped = Command::azimuthGlide(190.0f, 0.03f);
  CHECK_NEAR(wrapped.getAzimuthGlideTargetDegrees(), -170.0f, 15.0f);
}
