#ifndef _COMMAND_H_
#define _COMMAND_H_

#include "digit.h"

#include <string>
#include <string_view>
#include <cassert>

class Command {
 public:
  Command() = default;

  // Convenience for trusted callers (tests, and any other in-code string
  // literal) - setData() below is the fallible sibling for untrusted input
  // (Song::open(), loading a <command> element from a possibly hand-
  // edited/corrupted file), which needs to actually detect and react to
  // malformed data rather than have it silently become "----". A malformed
  // literal here asserts (a programmer typo, not runtime input); in a
  // Release build (assert compiled out) this Command is simply left
  // default-initialized to "----" instead, same fallback setData() itself
  // leaves in place on failure.
  Command(std::string_view values) {
    bool ok = setData(values);
    assert(ok);
    (void)ok; // otherwise unused in a Release build, where assert() is a no-op
  }

  // Parses `values` (exactly 4 characters) into this Command, character by
  // character through updateData() - same validation as live typed entry,
  // so data loaded from a file is held to exactly the same rules as data
  // typed through the UI, one authoritative definition of "valid" rather
  // than a second copy that could drift. Returns whether every character
  // was accepted; on failure this Command is left entirely unmodified
  // (not partially applied), so a caller can tell "malformed" apart from
  // "successfully parsed" and react accordingly (Song::open() rejects the
  // whole file rather than silently loading a "----" it never contained).
  bool setData(std::string_view values) {
    if (values.size() != 4) return false;
    Command parsed;
    for (int i = 0; i < 4; i++) {
      if (!parsed.updateData(i, values[static_cast<size_t>(i)])) return false;
    }
    *this = parsed;
    return true;
  }

  // Validates and stores a single raw InputEvent::getId() codepoint into
  // values_[i], reporting whether it was accepted so the caller (only
  // PatternEditor::offerInput()'s ColumnType::EFFECT branch) doesn't need
  // to separately pre-classify input.getId() before attempting this -
  // there's exactly one authoritative definition of "valid", here, not one
  // in the caller and a second one duplicated/drifting inside this class.
  // Column 0/1 (docs/commands.md's two-character mnemonic - ZB, 0U, 0D,
  // 0G, 1V, 1I, 1O, 1T, 2L, 2R) accepts [A-Za-z0-9-]; column 2/3 (the hex
  // argument) accepts the narrower [A-Fa-f0-9-]. Letters normalize to
  // uppercase ASCII on storage, same as a typed command always has (a
  // codepoint outside ASCII, e.g. a fullwidth Latin letter, is simply
  // invalid here - unlike digit()'s own fullwidth handling, which is for
  // *interpreting* an already-stored hex digit, not for command *entry*).
  // Explicit range checks rather than <cctype>'s toupper()/isalnum() -
  // same UB reasoning digit()'s own comment gives: a codepoint isn't
  // guaranteed representable as unsigned char/EOF, which their contract
  // requires.
  bool updateData(int i, int32_t codepoint) {
    if (i < 0 || i >= 4) return false;
    if (codepoint >= 'a' && codepoint <= 'z') codepoint -= 'a' - 'A'; // normalize once, up front, rather than testing lowercase separately below
    char hi = i < 2 ? 'Z' : 'F'; // mnemonic columns (0/1) allow the full alphabet; the hex argument ones (2/3) don't
    bool valid = codepoint == '-' || (codepoint >= '0' && codepoint <= '9') || (codepoint >= 'A' && codepoint <= hi);
    if (!valid) return false;
    values_[i] = static_cast<char>(codepoint);
    return true;
  }

  bool isDefined() const { return values_[0] != '-' || values_[1] != '-' || values_[2] != '-' || values_[3] != '-'; }

  // ZBxx ("Z" being the group every native/global command not tied to a
  // specific per-note effect lives under) - pattern break: jump straight
  // to row `xx` (see getBreakDestinationRow() below) of the *next*
  // pattern instead of playing out the rest of this one. See
  // docs/commands.md.
  bool isPatternBreak() const { return values_[0] == 'Z' && values_[1] == 'B'; }

  // The 2-hex-digit row argument for ZBxx (values_[2..3], 0-255) - a
  // non-hex character parses as digit 0 rather than being rejected
  // (digit() returns -1 for those). Live typed entry can no longer
  // actually produce one (updateData() validates columns 2/3 to
  // [A-Fa-f0-9-] up front), but a Command loaded via the string_view
  // constructor - straight from a hand-edited/malformed XML file -
  // bypasses updateData() entirely, so this stays permissive rather than
  // asserting on data this class didn't itself validate.
  int getBreakDestinationRow() const {
    auto hi = digit(values_[2], 16), lo = digit(values_[3], 16);
    return (hi < 0 ? 0 : hi) * 16 + (lo < 0 ? 0 : lo);
  }

  // 2Lxx/2Rxx - slide azimuth left/right: nudge the track's azimuth (and,
  // unlike a plain track-azimuth change, every currently-sounding voice's
  // own position too - see InstrumentTrackState::adjustAzimuth()) by `xx`
  // degrees per tick, for as long as this row lasts
  // (constants::TICKS_PER_ROW ticks/row - see SongState::
  // scheduleAzimuthSlide()). "2" groups azimuth commands the same way "0"
  // groups pitch and "1" groups volume-ish commands in docs/commands.md;
  // L/R match this engine's own azimuth sign convention (positive =
  // right, see SphericalPosition.h).
  bool isAzimuthSlide() const { return values_[0] == '2' && (values_[1] == 'L' || values_[1] == 'R'); }

  // Signed degrees-per-tick for isAzimuthSlide() (values_[2..3], same
  // permissive 2-hex-digit parsing as getBreakDestinationRow() above) -
  // negative for 2Lxx (left), positive for 2Rxx (right).
  float getAzimuthSlidePerTick() const {
    auto hi = digit(values_[2], 16), lo = digit(values_[3], 16);
    float magnitude = static_cast<float>((hi < 0 ? 0 : hi) * 16 + (lo < 0 ? 0 : lo));
    return values_[1] == 'L' ? -magnitude : magnitude;
  }

  const char * data() const { return &(values_[0]); }

 private:
  char values_[4] = { '-', '-', '-', '-' };
};

static inline const std::string to_string(const Command & command) {
  auto values = command.data();
  std::string s;
  s += values[0];
  s += values[1];
  s += values[2];
  s += values[3];
  return s;
}

#endif
