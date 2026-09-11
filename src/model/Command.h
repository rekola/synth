#ifndef _COMMAND_H_
#define _COMMAND_H_

#include "../util/digit.h"

#include <cmath>
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
  // Column 0/1 (docs/commands.md's two-character mnemonic - ZB, ZT, 0U,
  // 0D, 0G, 0V, 0I, 0O, 0T, 0H, 0K, 0L, 0F, 0M) accepts [A-Za-z0-9-];
  // column 2/3 (the hex argument) accepts the narrower [A-Fa-f0-9-].
  // Column 0 is either 'Z' (a global command, not scoped to any one
  // track/device - ZBxx pattern break, ZTxx tempo) or a leaf-track
  // chain-position digit, always '0' today since there's no equivalent
  // yet to Renoise's own numbered per-track DSP devices that digit
  // addresses there - see updateData()'s own '-'-as-'0' handling below.
  // Letters normalize to
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
    bool valid;
    if (i == 0) {
      // Column 0 is the device index (docs/commands.md) - 'Z' (a global
      // command) or a digit (how far up this track's own ancestor chain
      // the command targets, '-' included as a typed synonym for '0' -
      // see below). No other letter means anything here, unlike column
      // 1's own full mnemonic alphabet just below - accepting one would
      // silently store a command whose device index isn't actually any
      // of the values this class (or docs/commands.md) ever gives a
      // meaning to.
      valid = codepoint == '-' || codepoint == 'Z' || (codepoint >= '0' && codepoint <= '9');
    } else {
      char hi = i < 2 ? 'Z' : 'F'; // column 1 (the action letter) allows the full alphabet; the hex argument columns (2/3) don't
      valid = codepoint == '-' || (codepoint >= '0' && codepoint <= '9') || (codepoint >= 'A' && codepoint <= hi);
    }
    if (!valid) return false;
    // Every column *other* than 0 still stores a literal '-' when typed -
    // that's this class's own "not set" placeholder (values_'s
    // default-constructed value, isDefined()'s own check), unaffected by
    // column 0's own '-'-as-'0' normalization above.
    values_[i] = (i == 0 && codepoint == '-') ? '0' : static_cast<char>(codepoint);
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

  // 0Hxx/0Kxx - slide azimuth left/right: nudge the track's azimuth (and,
  // unlike a plain track-azimuth change, every currently-sounding voice's
  // own position too - see InstrumentTrackState::adjustAzimuth()) by `xx`
  // degrees per tick, for as long as this row lasts
  // (constants::TICKS_PER_ROW ticks/row - see SongState::
  // scheduleAzimuthSlide()). Column 0 is this engine's own leaf-track
  // chain-position digit (see updateData()'s own comment) - always '0'
  // today, since there's no equivalent yet to Renoise's own numbered
  // per-track DSP devices this same digit addresses there. "H"/"K":
  // Renoise has no azimuth-slide equivalent to match letters with
  // (its own panning is 2D, not this engine's full 3D positioning), but
  // "K" does line up with Renoise's own *panning-column* (a different,
  // note-row-scoped sub-column from this engine's single Command type)
  // "Kx" - pan slide right there too; "H" is otherwise unclaimed by any
  // Renoise command. (This pair used to be "L"/"R" under a now-retired
  // "2" group digit - moved once volume needed "0Lxx" for itself, see
  // isVolumeSet()'s own comment on why "L" won that fight.)
  bool isAzimuthSlide() const { return values_[0] == '0' && (values_[1] == 'H' || values_[1] == 'K'); }

  // Signed degrees-per-tick for isAzimuthSlide() (values_[2..3], same
  // permissive 2-hex-digit parsing as getBreakDestinationRow() above) -
  // negative for 0Hxx (left), positive for 0Kxx (right).
  float getAzimuthSlidePerTick() const {
    auto hi = digit(values_[2], 16), lo = digit(values_[3], 16);
    float magnitude = static_cast<float>((hi < 0 ? 0 : hi) * 16 + (lo < 0 ? 0 : lo));
    return values_[1] == 'H' ? -magnitude : magnitude;
  }

  // 0Pxx - set azimuth to an absolute position, unlike 0Hxx/0Kxx's own
  // relative nudge (the same Slide-vs-Set distinction as 0Lxx/0Fxx/0Mxx
  // have against them). "P" matches Renoise's own `0Pxx` "Track Pan"
  // command exactly (`xx` 00/80/FF = left/center/right there too) - a
  // real, deliberate limitation inherited along with the letter: `xx`
  // only reaches half this engine's own full 360-degree azimuth range
  // (-90 at 00 through +90 at FF, i.e. the front hemisphere only,
  // matching Renoise's own left-right stereo-pan metaphor, which has no
  // "behind" to reach in the first place) - this command simply can't
  // address a "behind" position the way 0Hxx/0Kxx's own relative slides
  // eventually can by accumulating past +-90 over several rows. Not a
  // bug to fix later so much as what borrowing a 2D-panner's own command
  // shape onto a 3D one necessarily costs.
  bool isAzimuthSet() const { return values_[0] == '0' && values_[1] == 'P'; }

  // xx (0-255, permissive 2-hex-digit parsing) maps linearly from -90
  // degrees at 00 through +90 at FF - see isAzimuthSet()'s own comment on
  // the half-circle limitation this inherits from Renoise's own encoding.
  float getAzimuthSetDegrees() const {
    auto hi = digit(values_[2], 16), lo = digit(values_[3], 16);
    auto magnitude = static_cast<float>((hi < 0 ? 0 : hi) * 16 + (lo < 0 ? 0 : lo));
    return -90.0f + (magnitude / 255.0f) * 180.0f;
  }

  // 0Lxx/0Fxx/0Mxx - set Volume (Send Main)/Send A/Send B to an absolute
  // level, unlike the slide commands above's own per-row relative nudge:
  // this is what a live-recorded fader move needs (the fader was *at*
  // this value at this moment, not "moved by some delta since whenever
  // the last one landed"), and what hand-typing an exact level directly
  // is naturally shaped like too. Column 0 is this engine's own
  // leaf-track chain-position digit (see updateData()'s own comment) -
  // always '0'. "L" for Level, matching Renoise's own `0Lxx` "Track
  // Level" command exactly (checked after the fact, not before - this
  // codebase's own stated convention is to check an established lineage
  // before inventing a mnemonic, which an earlier pass of this code
  // skipped twice in a row: the original pick, "S", collided with
  // Renoise's own long-established `Sxx` "Trigger Slice"; the group-digit
  // scheme itself, "1"/"4"/"5" for Volume/Send A/Send B, wrongly modeled
  // Renoise's own leading digit as a *category* selector when it's really
  // a *device-chain-position* one - always 0 here, since this engine has
  // no addressable per-track device chain to target a nonzero digit at).
  // "F"/"M" for Send A/Send B have no Renoise precedent to match at all -
  // sends there are DSP-chain routing, never a pattern command - so
  // they're just two letters Renoise itself doesn't already use for
  // anything, picked for that reason alone, not a borrowed meaning.
  bool isVolumeSet() const { return values_[0] == '0' && values_[1] == 'L'; }
  bool isSendASet() const { return values_[0] == '0' && values_[1] == 'F'; }
  bool isSendBSet() const { return values_[0] == '0' && values_[1] == 'M'; }

  // Shared decode for all three above - xx (0-255, permissive 2-hex-digit
  // parsing, same as getBreakDestinationRow()/getAzimuthSlidePerTick())
  // maps linearly in dB from -80dB (perceptually silent - not a true
  // hard-off floor like a fader's own bottom position, but close enough
  // that the distinction is inaudible) up to 0dB/unity at 255 - a finer,
  // self-contained curve deliberately independent of any one controller's
  // own coarser row grid (e.g. LaunchpadManager::sendRowToDb()'s 8-step
  // one), since a Command's own resolution isn't tied to how many rows
  // any particular hardware fader happens to have. Returns linear gain
  // directly (LeafTrackState::setSendMain()/setSendA()/setSendB()'s own
  // argument type), not dB - there's no other consumer that would want
  // the dB value itself.
  float getSendSetLinear() const {
    auto hi = digit(values_[2], 16), lo = digit(values_[3], 16);
    auto magnitude = static_cast<float>((hi < 0 ? 0 : hi) * 16 + (lo < 0 ? 0 : lo));
    float db = -80.0f + (magnitude / 255.0f) * 80.0f;
    return powf(10.0f, db * 0.05f);
  }

  // The inverse of getSendSetLinear() - builds a real 0Lxx/0Fxx/0Mxx
  // Command encoding `linear` (clamped into the representable -80..0dB
  // range first, same floor getSendSetLinear() itself decodes down to).
  // What live-recording a Launchpad fader move needs: capturing the
  // exact live value LeafTrackState::setSendMain()/etc. was just called
  // with, in the same units and curve a hand-typed 0Lxx/0Fxx/0Mxx
  // already round-trips through, not a second, independently-tuned
  // encoding.
  static Command volumeSet(float linear) { return makeSendSet('L', linear); }
  static Command sendASet(float linear) { return makeSendSet('F', linear); }
  static Command sendBSet(float linear) { return makeSendSet('M', linear); }

  const char * data() const { return &(values_[0]); }

 private:
  // volumeSet()/sendASet()/sendBSet()'s own shared builder - `letter` is
  // 'L'/'F'/'M', `linear` is clamped into [0dB-floor..unity] before
  // encoding (a value already out of that range, e.g. slightly above
  // unity from a plugin's own headroom, would otherwise silently wrap
  // into an unrelated magnitude via the plain float-to-int truncation
  // below - clamping first is what getSendSetLinear()'s own asymmetric
  // "at most 0dB" ceiling already implies is the representable range).
  static Command makeSendSet(char letter, float linear) {
    float db = linear <= 0.00001f ? -80.0f : 20.0f * log10f(linear);
    if (db < -80.0f) db = -80.0f;
    if (db > 0.0f) db = 0.0f;
    int magnitude = static_cast<int>(lround((db + 80.0f) / 80.0f * 255.0f));
    Command c;
    c.values_[0] = '0';
    c.values_[1] = letter;
    c.values_[2] = kHexDigits[(magnitude >> 4) & 0xF];
    c.values_[3] = kHexDigits[magnitude & 0xF];
    return c;
  }

  static constexpr char kHexDigits[] = "0123456789ABCDEF";

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
