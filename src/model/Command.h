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
  // 0D, 0G, 0V, 0I, 0O, 0T, 0L, 0F, 0M, 0P, YM, YA, YB, YL, YR, YZ)
  // accepts [A-Za-z0-9-]; column 2/3 (the hex argument) accepts the
  // narrower [A-Fa-f0-9-]. Column 0 is 'Z' (a global command, not scoped
  // to any one track/device - ZBxx pattern break, ZTxx tempo), 'Y' (this
  // engine's own reserved namespace - see isVolumeGlide()'s own comment),
  // or a leaf-track chain-position digit, always '0' today (no
  // addressable per-track device chain exists yet for a nonzero digit to
  // target) - see updateData()'s own '-'-as-'0' handling below. Letters
  // normalize to uppercase ASCII on storage, same as a typed command
  // always has (a codepoint outside ASCII, e.g. a fullwidth Latin letter,
  // is simply invalid here - unlike digit()'s own fullwidth handling,
  // which is for *interpreting* an already-stored hex digit, not for
  // command *entry*).
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
      // command), 'Y' (this engine's own reserved namespace - see
      // isVolumeGlide()'s own comment), or a digit (how far up this
      // track's own ancestor chain the command targets, '-' included as a
      // typed synonym for '0' - see below). No other letter means
      // anything here, unlike column 1's own full mnemonic alphabet just
      // below - accepting one would silently store a command whose device
      // index isn't actually any of the values this class (or
      // docs/commands.md) ever gives a meaning to.
      valid = codepoint == '-' || codepoint == 'Z' || codepoint == 'Y' || (codepoint >= '0' && codepoint <= '9');
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

  // YLxx/YRxx - slide azimuth left/right: nudge the track's azimuth
  // (and, unlike a plain track-azimuth change, every currently-sounding
  // voice's own position too - see InstrumentTrackState::
  // adjustAzimuth()) by `xx` degrees per tick, for as long as this row
  // lasts (constants::TICKS_PER_ROW ticks/row - see SongState::
  // scheduleAzimuthSlide()). Column 0 is 'Y' (see updateData()'s own
  // comment). See docs/commands.md's own Source column for where "L"/
  // "R" came from.
  bool isAzimuthSlide() const {
    return values_[0] == 'Y' && (values_[1] == 'L' || values_[1] == 'R');
  }

  // Signed degrees-per-tick for isAzimuthSlide() (values_[2..3], same
  // permissive 2-hex-digit parsing as getBreakDestinationRow() above) -
  // negative for left (YLxx), positive for right (YRxx).
  float getAzimuthSlidePerTick() const {
    auto hi = digit(values_[2], 16), lo = digit(values_[3], 16);
    float magnitude = static_cast<float>((hi < 0 ? 0 : hi) * 16 + (lo < 0 ? 0 : lo));
    return values_[1] == 'L' ? -magnitude : magnitude;
  }

  // 0Pxx - set azimuth to an absolute position, unlike YLxx/YRxx's own
  // relative nudge (the same Slide-vs-Set distinction as 0Lxx/0Fxx/0Mxx
  // have against them). A real, deliberate limitation: `xx` only reaches
  // half this engine's own full 360-degree azimuth range (-90 at 00
  // through +90 at FF, i.e. the front hemisphere only - a left-right
  // stereo-pan metaphor has no "behind" to reach in the first place) -
  // this command simply can't address a "behind" position the way
  // YLxx/YRxx's own relative slides eventually can by accumulating past
  // +-90 over several rows. See docs/commands.md's own Source column for
  // where "P" and this encoding came from.
  bool isAzimuthSet() const { return values_[0] == '0' && values_[1] == 'P'; }

  // xx (0-255, permissive 2-hex-digit parsing) maps linearly from -90
  // degrees at 00 through +90 at FF - see isAzimuthSet()'s own comment on
  // the half-circle limitation this encoding has.
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
  // always '0'. See docs/commands.md's own Source column for where "L"/
  // "F"/"M" came from.
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

  // YMxy/YAxy/YBxy - Volume (Send Main)/Send A/Send B's own equivalent of
  // 0Lxx/0Fxx/0Mxx above, but carrying a glide duration alongside the
  // target rather than an instant set: what a live-recorded Launchpad
  // fader move actually needs to reproduce the glide it performed, not
  // just where it ended up. `x` (values_[2]) is the target - the same
  // linear-in-dB curve as 0Lxx/0Fxx/0Mxx, just nibble (0-15) instead of
  // byte resolution; `y` (values_[3]) is the glide's own duration, real
  // (wall-clock) seconds, unaffected by tempo - a fader press's own
  // velocity-driven speed has nothing to do with it. Column 0 is 'Y' (see
  // updateData()'s own comment), not the leaf-track chain-position digit
  // 0Lxx/etc. use. See docs/commands.md's own "Recorded fader-glide
  // commands" section for the full encoding/rationale.
  bool isVolumeGlide() const { return values_[0] == 'Y' && values_[1] == 'M'; }
  bool isSendAGlide() const { return values_[0] == 'Y' && values_[1] == 'A'; }
  bool isSendBGlide() const { return values_[0] == 'Y' && values_[1] == 'B'; }

  // x (values_[2], one hex digit - a non-hex character parses as 0, same
  // as digit()'s own contract) maps linearly in dB, same -80dB..0dB span
  // getSendSetLinear() uses, at nibble instead of byte resolution.
  // Returns dB directly, not linear gain (unlike getSendSetLinear()) -
  // LeafTrackState::glideSendMain()/A()/B(), the consumer here, interpolates
  // in dB (see that class's own comment for why), so this skips a
  // pointless dB->linear->dB round trip.
  float getGlideTargetDb() const {
    auto x = digit(values_[2], 16);
    float magnitude = static_cast<float>(x < 0 ? 0 : x);
    return -80.0f + (magnitude / 15.0f) * 80.0f;
  }

  // y (values_[3], one hex digit, same permissive parsing) maps linearly
  // in real seconds from kMinFaderRampSeconds at 0 up to
  // kMaxFaderRampSeconds at 15 - the exact range a live Launchpad press's
  // own velocity-scaled duration already scales into
  // (LaunchpadManager::faderGlideDurationSeconds()), so a recorded move
  // reads on the same real-time scale a live one does.
  float getGlideDurationSeconds() const {
    auto y = digit(values_[3], 16);
    float step = static_cast<float>(y < 0 ? 0 : y);
    return kMinGlideSeconds + (step / 15.0f) * (kMaxGlideSeconds - kMinGlideSeconds);
  }

  // The inverse of getGlideTargetDb()/getGlideDurationSeconds() -
  // captures exactly what a Launchpad fader press just told the live
  // engine to do (LeafTrackState::glideSendMain()/etc.'s own target_db/
  // duration_seconds arguments), so a played-back YMxy/YAxy/YBxy
  // reproduces that same press, not a re-derived approximation of it.
  static Command volumeGlide(float target_db, float duration_seconds) { return makeGlideSet('M', target_db, duration_seconds); }
  static Command sendAGlide(float target_db, float duration_seconds) { return makeGlideSet('A', target_db, duration_seconds); }
  static Command sendBGlide(float target_db, float duration_seconds) { return makeGlideSet('B', target_db, duration_seconds); }

  // YZxy - azimuth's own equivalent of YMxy/YAxy/YBxy: an absolute target
  // with an explicit glide duration, what a live-recorded Launchpad Pan
  // press needs to reproduce the glide it performed. Not "YPxy" - 0Pxx's
  // own `xx` only reaches half the circle (isAzimuthSet()'s own comment on
  // why, a real inherited limitation there), which would throw away
  // exactly the range a Pan press can actually reach; `x` here instead
  // spans the *full* circle. `Z` for a**Z**imuth. Column 0 is 'Y' (see
  // updateData()'s own comment).
  bool isAzimuthGlide() const { return values_[0] == 'Y' && values_[1] == 'Z'; }

  // x (values_[2], one hex digit) maps linearly across the full circle,
  // -180 degrees at 0 up through +180 at 15 - independent of
  // azimuthToRow()/rowToAzimuth()'s own 8-row grid (a Command's own
  // resolution isn't tied to how many rows any particular hardware fader
  // happens to have, same reasoning getSendSetLinear()'s own comment
  // gives for Send).
  // The step is written out as a multiplication by an exactly-representable
  // 24 degrees rather than the arithmetically identical division by 15: a
  // reciprocal-approximated /15 leaves x=F a whisker past +180 instead of
  // exactly on it, and the far side of that boundary is a target half a
  // circle away once glideAzimuth() picks its own shorter direction.
  float getAzimuthGlideTargetDegrees() const {
    auto x = digit(values_[2], 16);
    float magnitude = static_cast<float>(x < 0 ? 0 : x);
    return magnitude * (360.0f / 15.0f) - 180.0f;
  }

  // The inverse of getAzimuthGlideTargetDegrees() - `duration_seconds`
  // shares getGlideDurationSeconds()'s own nibble encoding (values_[3]
  // isn't specific to any one of YMxy/YAxy/YBxy/YZxy, so that decoder
  // already works here unchanged).
  static Command azimuthGlide(float target_degrees, float duration_seconds) { return makeAzimuthGlideSet(target_degrees, duration_seconds); }

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

  // volumeGlide()/sendAGlide()/sendBGlide()'s own shared builder -
  // `letter` is 'M'/'A'/'B', `target_db` clamped into the representable
  // -80..0dB range first (same reasoning makeSendSet()'s own clamp has),
  // `duration_seconds` clamped into [kMinGlideSeconds, kMaxGlideSeconds]
  // before mapping into its own nibble.
  static Command makeGlideSet(char letter, float target_db, float duration_seconds) {
    float db = target_db;
    if (db < -80.0f) db = -80.0f;
    if (db > 0.0f) db = 0.0f;
    int target_nibble = static_cast<int>(lround((db + 80.0f) / 80.0f * 15.0f));
    float duration = duration_seconds;
    if (duration < kMinGlideSeconds) duration = kMinGlideSeconds;
    if (duration > kMaxGlideSeconds) duration = kMaxGlideSeconds;
    int duration_nibble = static_cast<int>(lround((duration - kMinGlideSeconds) / (kMaxGlideSeconds - kMinGlideSeconds) * 15.0f));
    Command c;
    c.values_[0] = 'Y';
    c.values_[1] = letter;
    c.values_[2] = kHexDigits[target_nibble & 0xF];
    c.values_[3] = kHexDigits[duration_nibble & 0xF];
    return c;
  }

  // azimuthGlide()'s own builder - same duration-nibble encoding
  // makeGlideSet() uses, but `target_degrees` wrapped into (-180,180]
  // first (a raw fmodf, not a clamp - unlike a dB target, degrees are
  // circular, so a value outside that range means "the same direction,
  // taken the long way round" rather than "out of representable range")
  // before mapping across the full circle.
  static Command makeAzimuthGlideSet(float target_degrees, float duration_seconds) {
    float wrapped = fmodf(target_degrees + 180.0f, 360.0f);
    if (wrapped < 0.0f) wrapped += 360.0f;
    wrapped -= 180.0f; // now in [-180, 180)
    int target_nibble = static_cast<int>(lround((wrapped + 180.0f) / 360.0f * 15.0f));
    float duration = duration_seconds;
    if (duration < kMinGlideSeconds) duration = kMinGlideSeconds;
    if (duration > kMaxGlideSeconds) duration = kMaxGlideSeconds;
    int duration_nibble = static_cast<int>(lround((duration - kMinGlideSeconds) / (kMaxGlideSeconds - kMinGlideSeconds) * 15.0f));
    Command c;
    c.values_[0] = 'Y';
    c.values_[1] = 'Z';
    c.values_[2] = kHexDigits[target_nibble & 0xF];
    c.values_[3] = kHexDigits[duration_nibble & 0xF];
    return c;
  }

  static constexpr char kHexDigits[] = "0123456789ABCDEF";

  // Deliberately duplicated, not shared with LaunchpadManager.cpp's own
  // identically-valued kMinFaderRampSeconds/kMaxFaderRampSeconds - this is
  // a model-layer class (src/model/), which src/launchpad/ already
  // depends on, not the other way around; the same "each file keeps its
  // own small constant" convention this class's own dB helpers already
  // follow. A live press's own duration (LaunchpadManager::
  // faderGlideDurationSeconds()) and a recorded one (getGlideDurationSeconds()
  // above) have to agree on this range for the two to read as the same
  // real-time scale.
  static constexpr float kMinGlideSeconds = 0.03f;
  static constexpr float kMaxGlideSeconds = 1.0f;

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
