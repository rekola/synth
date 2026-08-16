#include "TestFramework.h"

#include "../src/LaunchpadProtocol.h"

using namespace std;
using namespace LaunchpadProtocol;

TEST(model_info_matches_documented_per_model_capabilities) {
  auto mini = getModelInfo(Model::MINI_MK3);
  CHECK(mini.device_id == 0x0D);
  CHECK(!mini.velocity_sensitive);
  CHECK(!mini.poly_aftertouch);
  CHECK(mini.max_led_colourspecs == 81);

  auto x = getModelInfo(Model::X);
  CHECK(x.device_id == 0x0C);
  CHECK(x.velocity_sensitive);
  CHECK(x.poly_aftertouch);
  CHECK(x.max_led_colourspecs == 81);

  auto pro = getModelInfo(Model::PRO_MK3);
  CHECK(pro.device_id == 0x0E);
  CHECK(pro.velocity_sensitive);
  CHECK(pro.poly_aftertouch);
  CHECK(pro.max_led_colourspecs == 106);
}

TEST(model_from_device_name_matches_each_known_model) {
  CHECK(modelFromDeviceName("Launchpad Mini MK3 MIDI 2") == Model::MINI_MK3);
  CHECK(modelFromDeviceName("Launchpad X MIDI 2") == Model::X);
  CHECK(modelFromDeviceName("Launchpad Pro MK3 MIDI 2") == Model::PRO_MK3);
  CHECK(modelFromDeviceName("Some Other Device") == nullopt);
}

TEST(device_inquiry_request_matches_the_universal_sysex) {
  vector<uint8_t> expected = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
  CHECK(buildDeviceInquiry() == expected);
}

TEST(plausible_launchpad_reply_accepts_the_documented_application_reply) {
  vector<uint8_t> reply = {0xF0, 0x7E, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0x00, 0x00, 1, 2, 3, 4, 0xF7};
  CHECK(isPlausibleLaunchpadReply(reply));
}

TEST(plausible_launchpad_reply_rejects_unrelated_bytes) {
  vector<uint8_t> reply = {0xF0, 0x7E, 0x00, 0x06, 0x02, 0x00, 0x01, 0x02, 0x00, 0x00, 0xF7};
  CHECK(!isPlausibleLaunchpadReply(reply));
  CHECK(!isPlausibleLaunchpadReply({}));
}

TEST(programmer_mode_enter_uses_the_dedicated_toggle_for_x_and_mini) {
  vector<uint8_t> expected_x = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7};
  CHECK(buildProgrammerModeEnter(Model::X) == expected_x);

  vector<uint8_t> expected_mini = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x01, 0xF7};
  CHECK(buildProgrammerModeEnter(Model::MINI_MK3) == expected_mini);
}

TEST(programmer_mode_enter_uses_the_layout_select_message_for_pro_mk3) {
  vector<uint8_t> expected_pro = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x00, 0x11, 0x00, 0xF7};
  CHECK(buildProgrammerModeEnter(Model::PRO_MK3) == expected_pro);
}

TEST(pad_to_note_number_matches_the_documented_grid_numbering) {
  CHECK(padToNoteNumber(0, 0) == 11); // bottom-left
  CHECK(padToNoteNumber(7, 0) == 18); // bottom-right
  CHECK(padToNoteNumber(0, 7) == 81); // top-left
  CHECK(padToNoteNumber(7, 7) == 88); // top-right
}

TEST(note_number_to_pad_round_trips_and_rejects_out_of_range_notes) {
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      auto pad = noteNumberToPad(padToNoteNumber(x, y));
      CHECK(pad.has_value());
      CHECK(pad->first == x);
      CHECK(pad->second == y);
    }
  }
  CHECK(!noteNumberToPad(19).has_value()); // CC-mapped button, not a grid note
  CHECK(!noteNumberToPad(99).has_value());
  CHECK(!noteNumberToPad(5).has_value());
}

TEST(rgb_led_sysex_encodes_one_colourspec_per_pad) {
  vector<PadColor> colors = {
    {padToNoteNumber(0, 0), 127, 0, 0},
    {padToNoteNumber(1, 0), 0, 127, 0},
  };
  auto message = buildRgbLedSysEx(Model::X, colors);
  vector<uint8_t> expected = {
    0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03,
    0x03, 11, 127, 0, 0,
    0x03, 12, 0, 127, 0,
    0xF7
  };
  CHECK(message == expected);
}

// This is the "mock MIDI device with scripted velocity/aftertouch streams"
// hardware-free testing strategy: a plain vector<uint8_t> stands in for
// what a real Launchpad would send, decoded via a pure function - no
// transport object, no ALSA, no mock class needed.
TEST(decode_incoming_bytes_handles_a_press_hold_aftertouch_release_sequence) {
  vector<uint8_t> stream = {
    0x90, 11, 100,  // press pad (0,0), velocity 100
    0xA0, 11, 40,   // aftertouch on pad (0,0), pressure 40
    0xA0, 11, 90,   // aftertouch on pad (0,0), pressure 90 (increasing)
    0x90, 11, 0,    // note-on with velocity 0 == release (per Novation docs)
  };
  auto events = decodeIncomingBytes(Model::X, stream);
  CHECK(events.size() == 4);
  CHECK(events[0].x == 0 && events[0].y == 0 && events[0].kind == EventKind::PRESS && events[0].velocity == 100);
  CHECK(events[1].kind == EventKind::AFTERTOUCH && events[1].velocity == 40);
  CHECK(events[2].kind == EventKind::AFTERTOUCH && events[2].velocity == 90);
  CHECK(events[3].kind == EventKind::RELEASE);
}

TEST(decode_incoming_bytes_accepts_explicit_note_off_too) {
  vector<uint8_t> stream = {0x80, 18, 0};
  auto events = decodeIncomingBytes(Model::MINI_MK3, stream);
  CHECK(events.size() == 1);
  CHECK(events[0].x == 7 && events[0].y == 0 && events[0].kind == EventKind::RELEASE);
}

TEST(decode_incoming_bytes_skips_embedded_sysex_and_ignores_non_grid_notes) {
  vector<uint8_t> stream = {
    0xF0, 0x7E, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0x00, 0x00, 1, 2, 3, 4, 0xF7,
    0x90, 88, 127, // top-right pad press
    0xB0, 91, 5,   // a CC message (top-row button) - not decoded as a pad event
  };
  auto events = decodeIncomingBytes(Model::X, stream);
  CHECK(events.size() == 1);
  CHECK(events[0].x == 7 && events[0].y == 7 && events[0].kind == EventKind::PRESS && events[0].velocity == 127);
}

TEST(command_for_button_returns_the_assigned_command_names) {
  CHECK(commandForButton(91) == string("move-row-up"));
  CHECK(commandForButton(92) == string("move-row-down"));
  CHECK(commandForButton(93) == string("prev-track"));
  CHECK(commandForButton(94) == string("next-track"));
  CHECK(commandForButton(30) == string("toggle-mute"));  // Pro MK3 left column
  CHECK(commandForButton(20) == string("toggle-solo"));  // Pro MK3 left column
  CHECK(commandForButton(39) == string("toggle-mute"));  // right column - inferred Launchpad X alias, see LaunchpadProtocol.cpp
  CHECK(commandForButton(29) == string("toggle-solo"));  // right column - inferred Launchpad X alias, see LaunchpadProtocol.cpp
}

TEST(command_for_button_returns_nullopt_for_reserved_and_out_of_range_ccs) {
  CHECK(commandForButton(95) == nullopt);
  CHECK(commandForButton(96) == nullopt);
  CHECK(commandForButton(97) == nullopt);
  CHECK(commandForButton(99) == nullopt);
  CHECK(commandForButton(19) == nullopt); // right column - reserved (Record Arm)
  CHECK(commandForButton(49) == nullopt); // right column - reserved (Stop Clip)
  // 59/69/79/89 (Send B/Send A/Pan/Volume mode buttons) are deliberately
  // absent here too - they're intercepted by raw CC number in
  // LaunchpadManager::handleRawButton, before commandForButton is ever
  // consulted (see UI::handleLaunchpadButtonEvent), since pressing one only
  // flips a device's own transient UI state, never a "command".
  CHECK(commandForButton(59) == nullopt);
  CHECK(commandForButton(69) == nullopt);
  CHECK(commandForButton(79) == nullopt);
  CHECK(commandForButton(89) == nullopt);
  // 98 (Capture MIDI, top row 8) is likewise intercepted by raw CC number
  // in LaunchpadManager::handleRawButton (a per-device record-arm toggle,
  // not a Song/Track-mutating command) - see its own comment.
  CHECK(commandForButton(98) == nullopt);
  CHECK(commandForButton(10) == nullopt); // Pro MK3 left column - unassigned position
  CHECK(commandForButton(101) == nullopt); // Pro MK3 bottom row - not assigned this pass
  CHECK(commandForButton(0) == nullopt);
  CHECK(commandForButton(-1) == nullopt);
}

TEST(is_pro_mk3_only_led_index_identifies_the_25_exclusive_buttons) {
  // Left column
  for (int cc : {10, 20, 30, 40, 50, 60, 70, 80, 90}) CHECK(isProMk3OnlyLedIndex(cc));
  // Track select row
  for (int cc = 101; cc <= 108; cc++) CHECK(isProMk3OnlyLedIndex(cc));
  // Track control row
  for (int cc = 1; cc <= 8; cc++) CHECK(isProMk3OnlyLedIndex(cc));

  // Shared across all 3 models: grid notes, top row, right column, corner
  for (int x = 0; x < 8; x++) {
    for (int y = 0; y < 8; y++) CHECK(!isProMk3OnlyLedIndex(padToNoteNumber(x, y)));
  }
  for (int cc = 91; cc <= 99; cc++) CHECK(!isProMk3OnlyLedIndex(cc));
  for (int cc = 19; cc <= 89; cc += 10) CHECK(!isProMk3OnlyLedIndex(cc));
}
