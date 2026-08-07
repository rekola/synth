#ifndef _COMMAND_H_
#define _COMMAND_H_

#include "digit.h"

#include <string>
#include <string_view>
#include <cassert>

class Command {
 public:
  Command() {
    values_[0] = '-';
    values_[1] = '-';
    values_[2] = '-';
    values_[3] = '-';
  }

  Command(std::string_view values) {
    assert(values.size() == 4);
    if (values.size() >= 4) {
      values_[0] = values[0];
      values_[1] = values[1];
      values_[2] = values[2];
      values_[3] = values[3];
    }
  }

  void updateData(size_t i, char c) { if (i < 4) values_[i] = c; }

  bool isDefined() const { return values_[0] != '-' || values_[1] != '-' || values_[2] != '-' || values_[3] != '-'; }

  // ZBxx ("Z" being the group every native/global command not tied to a
  // specific per-note effect lives under) - pattern break: jump straight
  // to row `xx` (see getBreakDestinationRow() below) of the *next*
  // pattern instead of playing out the rest of this one. See
  // docs/commands.md.
  bool isPatternBreak() const { return values_[0] == 'Z' && values_[1] == 'B'; }

  // The 2-hex-digit row argument for ZBxx (values_[2..3], 0-255) - a
  // non-hex character (the effect column accepts any letter, not just
  // a-f - see docs/commands.md) parses as digit 0 rather than being
  // rejected (digit() returns -1 for those), same permissiveness as every
  // other effect-column entry.
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
  char values_[4];
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
