#include "EscapeCoalescer.h"

void
KittyProtocolDetector::observe(ncintype_e evtype) {
  if (settled_) return;
  active_ = (evtype != NCTYPE_UNKNOWN);
  settled_ = true;
}

EscapeSequenceCoalescer::EscapeSequenceCoalescer(InputEventSource & source, KittyProtocolDetector & detector)
  : source_(source), detector_(detector) {
}

void
EscapeSequenceCoalescer::setInPaste(bool in_paste) {
  in_paste_ = in_paste;
  if (in_paste_) pending_ = false; // a paste's first character isn't retroactively Alt-chorded
}

namespace {

// '[' (CSI) and 'O' (SS3) are the two legacy escape-sequence introducer
// bytes - a real Alt-[ or Alt-O keypress encodes identically to their
// sequence-introducer reading, the same ambiguity that already exists
// without this coalescer (see TerminalUI.cpp's readInput() and its own
// SS3-sequence recognizer for a concrete example depending on seeing an
// un-modified 'O' after ESC: ESC 'O' '5' 'o'/'j', each arriving as its
// own separate event). Declining to fold them changes nothing that
// worked before this coalescer existed.
bool
isSequenceIntroducer(const ncinput & ni) {
  return ni.id == '[' || ni.id == 'O';
}

// A keystroke that already carries Ctrl, Shift, or Meta is never folded
// either - not an ambiguity this time, but a deliberate choice mirroring
// notcurses's own native ESC+letter merge (walk_automaton() in
// automaton.c - see normalizeLegacyAlt()'s own comment): that merge only
// ever represents a plain Alt chord with no other modifier riding along,
// never Alt combined with anything else. Nothing in this codebase binds a
// genuine Ctrl+Alt or Shift+Alt chord either, while Ctrl-B/-W/-Y/-G and
// Shift's own role in raw note entry (starting a new chord column - see
// PatternEditor.cpp's input.hasShift() branch) are exact-match on Alt
// being *unset*. With no deadline, a pending Escape can sit open for as
// long as a person takes to press the next key - long enough that an
// incidental Escape (a habit left over from elsewhere, or simply changing
// their mind) followed by ordinary continued use, Ctrl-G or a
// Shift-modified note included, is a real scenario, not just a
// theoretical one. Folding Alt onto that next keystroke regardless of
// what it turned out to be would silently break any of them until some
// later unmodified keypress happened to absorb the fold instead.
bool
isAlreadyModified(const ncinput & ni) {
  return ncinput_ctrl_p(&ni) || ncinput_shift_p(&ni) || ncinput_meta_p(&ni);
}

// Events that don't correspond to a keyboard keystroke at all - a mouse
// press/release, or a synthetic resize - say nothing about whether a
// pending Escape's wait should end; they're noise with respect to
// "waiting for the next thing the user types".
bool
isKeyboardEvent(const ncinput & ni) {
  if (ni.evtype == NCTYPE_RELEASE) return false;
  if (nckey_mouse_p(ni.id)) return false;
  if (ni.id == NCKEY_RESIZE) return false;
  return true;
}

bool
isUnmodifiedEscapePress(const ncinput & ni) {
  return ni.id == NCKEY_ESC && ni.evtype != NCTYPE_RELEASE && ni.evtype != NCTYPE_REPEAT && !ncinput_alt_p(&ni);
}

// notcurses's own legacy-Alt handling (walk_automaton() in automaton.c):
// when ESC is immediately followed, in the same read, by a byte that
// doesn't extend any known multi-byte escape sequence (an ordinary
// Alt-modified letter, e.g. Alt-w - as opposed to Alt-[ or Alt-O, which
// really are still ambiguous with a genuine CSI/SS3 introducer and do
// still arrive as separate events for next() to coalesce below),
// notcurses already merges the two bytes into a single ncinput for that
// candidate byte - it never surfaces the ESC as its own event at all, so
// next() below never even gets a bare ESC to coalesce for that case. But
// that merge is recorded only via the deprecated `ncinput.alt` bool
// (confirmed against the installed 3.0.17 source) - it never touches the
// modern `modifiers` bitmask, so ncinput_alt_p() (and every exact-match
// keybinding in this codebase, e.g. PatternEditor's Alt-W) would never
// see it otherwise. Normalized into `modifiers` here, the one place a raw
// notcurses event is examined before anything else - see this
// component's own header comment on why `.alt` isn't read anywhere else.
void
normalizeLegacyAlt(ncinput & ni) {
  if (ni.alt && !ncinput_alt_p(&ni)) ni.modifiers |= NCKEY_MOD_ALT;
}

// Legacy terminals encode Ctrl-Space as a literal NUL byte. Every *other*
// C0 control byte (Ctrl-A through Ctrl-Z, values 1-26) gets converted by
// notcurses's own load_ncinput() (in.c) into the corresponding letter with
// NCKEY_MOD_CTRL set - but that function's range check is `id > 0 && id <=
// 26`, deliberately excluding 0 (confirmed against the installed 3.0.17
// source), so Ctrl-Space alone arrives as a bare, unmodified codepoint 0
// instead. This codebase's own Ctrl-Space keybinding (`KeyChord::pack(' ',
// true, ...)`, PatternEditor.cpp's set-mark) - like a Kitty-protocol
// terminal's own native report of the same physical chord, which sends
// the actual codepoint (32) with the modifier bit set, never 0 - expects
// id=' ' with NCKEY_MOD_CTRL, so this fills the one gap load_ncinput()
// itself leaves in that otherwise-uniform conversion.
void
normalizeCtrlSpace(ncinput & ni) {
  if (ni.id == 0 && !ncinput_ctrl_p(&ni)) {
    ni.id = ' ';
    ni.modifiers |= NCKEY_MOD_CTRL;
  }
}

}

bool
EscapeSequenceCoalescer::next(ncinput * ni, bool definitely_pending) {
  if (!source_.next(ni, definitely_pending)) return false;

  normalizeLegacyAlt(*ni);
  normalizeCtrlSpace(*ni);

  // Mouse press/release always carries a real NCTYPE_PRESS/NCTYPE_RELEASE
  // (SGR mouse reporting distinguishes them unconditionally - confirmed
  // against notcurses's own mouse_click() in in.c, which sets evtype
  // itself before load_ncinput() ever gets a chance to apply its
  // Kitty-protocol-conditional UNKNOWN->PRESS backfill), completely
  // independent of whether the Kitty keyboard protocol is active. Feeding
  // a mouse event's evtype to the detector would misread the session's
  // very first mouse click - clicking into the pattern editor to focus
  // it, say - as proof of Kitty-protocol support, latching coalescing off
  // for every keystroke afterward on a terminal that never actually
  // negotiated it. Only a genuine keyboard event says anything about the
  // protocol.
  if (!nckey_mouse_p(ni->id)) detector_.observe(ni->evtype);

  if (detector_.active() || in_paste_) {
    pending_ = false;
    return true;
  }

  if (pending_) {
    if (!isKeyboardEvent(*ni)) return true; // noise - keep waiting
    pending_ = false;
    if (isUnmodifiedEscapePress(*ni)) {
      pending_ = true; // another bare Escape just restarts the wait
    } else if (!isSequenceIntroducer(*ni) && !isAlreadyModified(*ni)) {
      ni->modifiers |= NCKEY_MOD_ALT;
    }
    // else: a CSI/SS3 introducer byte, or an already Ctrl/Shift/Meta-
    // modified keystroke - declined, passed through as-is, and (like any
    // other real keyboard event) ends the wait.
    return true;
  }

  if (isUnmodifiedEscapePress(*ni)) pending_ = true;
  return true;
}
