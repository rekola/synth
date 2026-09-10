#ifndef _LAUNCHPADPROTOCOL_H_
#define _LAUNCHPADPROTOCOL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Novation Launchpad Programmer Mode SysEx/MIDI protocol: per-model constant
// data and pure encode/decode functions, no I/O. Byte sequences below are
// taken directly from Novation's published Programmer's Reference manuals
// for each model. Pure computation, no device dependency - the ALSA-facing
// code that actually sends/receives these bytes lives elsewhere (LaunchpadIO).
namespace LaunchpadProtocol {

  enum class Model {
    MINI_MK3,
    X,
    PRO_MK3
  };

  struct ModelInfo {
    uint8_t device_id;       // the byte after "F0 00 20 29 02" in every SysEx
    bool velocity_sensitive;
    bool poly_aftertouch;
    int grid_width = 8, grid_height = 8; // the core pad grid; all 3 models today
    int max_led_colourspecs;  // per LED-lighting SysEx message
  };

  ModelInfo getModelInfo(Model model);

  // Matches an ALSA sequencer client/port name (e.g. "Launchpad X MIDI 2")
  // against a known model. This is the PRIMARY identification mechanism -
  // Device Inquiry's reply bytes are documented identically across all
  // three models, so they cannot reliably discriminate model on their own
  // (see isPlausibleLaunchpadReply below).
  std::optional<Model> modelFromDeviceName(const std::string & name);

  // Universal Device Inquiry request, and a *non-model-discriminating*
  // sanity check on the reply: confirms the reply looks like some Launchpad
  // MK3-family device (manufacturer id 00 20 29, family code 13 01), for use
  // as a fire-and-forget confirmation - never as the basis for selecting a
  // model, and never something a caller should block waiting for.
  std::vector<uint8_t> buildDeviceInquiry();
  bool isPlausibleLaunchpadReply(const std::vector<uint8_t> & reply);

  // Enters Programmer Mode. Mini MK3/X use a dedicated mode-toggle message;
  // Pro MK3 has no such message and uses the general layout-select message
  // instead (layout 0x11 = Programmer Mode) - this function hides that
  // per-model structural difference.
  std::vector<uint8_t> buildProgrammerModeEnter(Model model);

  // The LED-lighting SysEx's own per-pad "lighting type" byte (Novation's
  // own numbering, not reorderable) - PadColor's r/g/b/palette/flash_to/
  // flash_from fields below are read/ignored according to this, mirroring
  // the protocol's own "one lighting type, one matching data shape" rule.
  // STATIC_PALETTE (a single palette-index byte) isn't offered - nothing in
  // this codebase colors a pad from the fixed 128-entry palette by index
  // rather than by RGB except FLASH/PULSE, which have no static-color
  // equivalent to fall back on anyway.
  enum class LightingType : uint8_t { FLASH = 1, PULSE = 2, STATIC_RGB = 3 };

  struct PadColor {
    int led_index; // as returned by padToNoteNumber
    uint8_t r = 0, g = 0, b = 0; // STATIC_RGB's Red/Green/Blue (0-127 each) -
                                 // the only fields this struct had before
                                 // FLASH/PULSE existed, so every existing
                                 // call site building one of these with just
                                 // {led_index, r, g, b} keeps compiling
                                 // unchanged and still means STATIC_RGB.
    LightingType type = LightingType::STATIC_RGB;
    uint8_t palette = 0; // PULSE's own single palette-index entry
    uint8_t flash_to = 0, flash_from = 0; // FLASH's own two palette-index
                                           // entries - the protocol's own
                                           // "Colour B"/"Colour A", sent in
                                           // that order (the color a flash
                                           // lands on, then the color it
                                           // flashes back to)
  };

  // Builds one combined LED-lighting SysEx message, mixing any combination
  // of static/flashing/pulsing pads (PadColor::type) in a single message.
  // Callers should keep the colourspec count within
  // getModelInfo(model).max_led_colourspecs.
  std::vector<uint8_t> buildLedLightingSysEx(Model model, const std::vector<PadColor> & colors);

  // The "11 + x + 10*y" grid numbering, identical across all three models
  // for the core 8x8 grid (x, y both 0-indexed from the bottom-left).
  int padToNoteNumber(int x, int y);
  std::optional<std::pair<int, int>> noteNumberToPad(int note);

  enum class EventKind { PRESS, RELEASE, AFTERTOUCH };

  struct RawEvent {
    int x, y;
    EventKind kind;
    int velocity; // 0-127; press velocity, or aftertouch pressure
  };

  // Decodes a buffer of raw incoming MIDI bytes into pad events. Assumes no
  // running status (each channel-voice message carries its own status
  // byte, which is how Launchpads send data in practice); SysEx blocks
  // (F0 ... F7) embedded in the same buffer are skipped rather than
  // misparsed as channel-voice data.
  std::vector<RawEvent> decodeIncomingBytes(Model model, const std::vector<uint8_t> & bytes);

  // Static, hardcoded CC-number -> command-name assignment for the extra
  // buttons outside the 8x8 grid (see the Launchpad follow-up plan). CC
  // numbers are identical across all three models for the shared top-row/
  // right-column/corner set; Pro MK3-only CC numbers (left column, the two
  // rows below the grid) simply aren't in this table except for the two
  // assigned Mute/Solo slots - X/Mini MK3 physically can't send those CCs,
  // so they're unreachable there rather than needing per-model gating.
  // Returns nullopt for unassigned/reserved CC numbers.
  std::optional<std::string> commandForButton(int cc_number);

  // True for CC numbers that only exist on Pro MK3 (its left column and the
  // two rows below the grid) - X/Mini MK3 have no physical button there.
  // LED-sending code should filter these out for non-Pro-MK3 sessions:
  // X/Mini MK3's documented "up to 81 colourspecs" limit is exactly
  // 64 grid pads + the 17 CC numbers shared by all three models, and
  // including Pro-MK3-only indices would exceed it.
  bool isProMk3OnlyLedIndex(int led_index);

}

#endif
