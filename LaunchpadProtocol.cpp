#include "LaunchpadProtocol.h"

using namespace std;

namespace LaunchpadProtocol {

ModelInfo
getModelInfo(Model model) {
  switch (model) {
  case Model::MINI_MK3:
    return ModelInfo{0x0D, false, false, 8, 8, 81};
  case Model::X:
    return ModelInfo{0x0C, true, true, 8, 8, 81};
  case Model::PRO_MK3:
    return ModelInfo{0x0E, true, true, 8, 8, 106};
  }
  return ModelInfo{0, false, false, 8, 8, 0};
}

optional<Model>
modelFromDeviceName(const string & name) {
  // Longer, more specific names first - none of these are substrings of
  // each other, but checking the more specific ones first keeps the intent
  // clear if similarly-named future models are ever added.
  if (name.find("Launchpad Mini MK3") != string::npos) return Model::MINI_MK3;
  if (name.find("Launchpad Pro MK3") != string::npos) return Model::PRO_MK3;
  if (name.find("Launchpad X") != string::npos) return Model::X;
  return nullopt;
}

vector<uint8_t>
buildDeviceInquiry() {
  return {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
}

bool
isPlausibleLaunchpadReply(const vector<uint8_t> & reply) {
  // F0 7E <channel> 06 02 00 20 29 13 01 ... F7 - only the manufacturer id
  // (00 20 29) and family code (13 01) are checked; these are documented
  // identically across Mini MK3/X/Pro MK3, so this cannot discriminate
  // model - it only confirms "some Launchpad MK3-family device answered".
  if (reply.size() < 10) return false;
  if (reply[0] != 0xF0 || reply[1] != 0x7E || reply[3] != 0x06 || reply[4] != 0x02) return false;
  if (reply[5] != 0x00 || reply[6] != 0x20 || reply[7] != 0x29) return false;
  if (reply[8] != 0x13 || reply[9] != 0x01) return false;
  return true;
}

vector<uint8_t>
buildProgrammerModeEnter(Model model) {
  auto device_id = getModelInfo(model).device_id;
  if (model == Model::PRO_MK3) {
    // No dedicated Programmer/Live toggle - use the general layout-select
    // message instead, with layout 0x11 (17 decimal) = Programmer Mode,
    // page 0x00 (only page that applies to non-paged views).
    return {0xF0, 0x00, 0x20, 0x29, 0x02, device_id, 0x00, 0x11, 0x00, 0xF7};
  }
  return {0xF0, 0x00, 0x20, 0x29, 0x02, device_id, 0x0E, 0x01, 0xF7};
}

vector<uint8_t>
buildRgbLedSysEx(Model model, const vector<PadColor> & colors) {
  vector<uint8_t> message = {0xF0, 0x00, 0x20, 0x29, 0x02, getModelInfo(model).device_id, 0x03};
  for (auto & color : colors) {
    message.push_back(0x03); // lighting type 3: RGB
    message.push_back(static_cast<uint8_t>(color.led_index));
    message.push_back(color.r);
    message.push_back(color.g);
    message.push_back(color.b);
  }
  message.push_back(0xF7);
  return message;
}

int
padToNoteNumber(int x, int y) {
  return 11 + x + 10 * y;
}

optional<pair<int, int>>
noteNumberToPad(int note) {
  auto offset = note - 11;
  if (offset < 0) return nullopt;
  auto x = offset % 10;
  auto y = offset / 10;
  if (x < 0 || x > 7 || y < 0 || y > 7) return nullopt;
  return make_pair(x, y);
}

vector<RawEvent>
decodeIncomingBytes(Model /*model*/, const vector<uint8_t> & bytes) {
  vector<RawEvent> events;

  size_t i = 0;
  while (i < bytes.size()) {
    auto status = bytes[i];

    if (status == 0xF0) {
      // Skip an embedded SysEx block entirely rather than misparsing it.
      while (i < bytes.size() && bytes[i] != 0xF7) i++;
      if (i < bytes.size()) i++; // consume the trailing F7
      continue;
    }

    if (status < 0x80) {
      // Not a status byte (would imply running status, which Launchpads
      // don't use in practice) - skip it defensively rather than looping.
      i++;
      continue;
    }

    auto high_nibble = static_cast<uint8_t>(status & 0xF0);
    if ((high_nibble == 0x80 || high_nibble == 0x90 || high_nibble == 0xA0) && i + 2 < bytes.size()) {
      auto note = bytes[i + 1];
      auto velocity = bytes[i + 2];
      auto pad = noteNumberToPad(note);
      if (pad) {
        EventKind kind;
        if (high_nibble == 0xA0) kind = EventKind::AFTERTOUCH;
        else if (high_nibble == 0x80 || velocity == 0) kind = EventKind::RELEASE;
        else kind = EventKind::PRESS;
        events.push_back({pad->first, pad->second, kind, velocity});
      }
      i += 3;
    } else {
      i++;
    }
  }

  return events;
}

bool
isProMk3OnlyLedIndex(int led_index) {
  switch (led_index) {
  case 10: case 20: case 30: case 40: case 50: case 60: case 70: case 80: case 90:
    return true; // left column
  default:
    if (led_index >= 101 && led_index <= 108) return true; // "track select" row
    if (led_index >= 1 && led_index <= 8) return true;     // "track control" row
    return false;
  }
}

optional<string>
commandForButton(int cc_number) {
  switch (cc_number) {
  case 91: return string("octave-up");    // top row 1, printed with an up-arrow icon
  case 92: return string("octave-down");  // top row 2, printed with a down-arrow icon
  case 93: return string("prev-track");   // top row 3
  case 94: return string("next-track");   // top row 4
  case 98: return string("toggle-playing"); // top row 8, printed with a record-circle icon
  case 30: return string("toggle-mute");  // Pro MK3 left column, position 6
  case 20: return string("toggle-solo");  // Pro MK3 left column, position 7
  default: return nullopt; // CC95-97, 99, the whole right column, and all other left-column/bottom-row buttons: reserved for now
  }
}

}
