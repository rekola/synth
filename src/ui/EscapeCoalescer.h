#ifndef _ESCAPECOALESCER_H_
#define _ESCAPECOALESCER_H_

#include <notcurses/notcurses.h>

// Coalesces the legacy Meta/Alt-key encoding notcurses reports as two
// independent events - a plain NCKEY_ESC followed by the modified key's
// own byte(s), with no guarantee they arrive close together in time - into
// a single Alt-modified event, matching what a terminal running the Kitty
// keyboard disambiguation protocol (CSI <cp> ; <mods> u) already sends
// directly. Modeled on Emacs's own ESC-as-Meta-prefix behavior (confirmed
// against real Emacs: pressing Escape, waiting indefinitely - seconds, not
// milliseconds - then pressing a key still produces the Meta-modified
// command; Emacs shows "ESC-" in the echo area the whole time it's
// waiting) rather than on a short timing window: a bare Escape arms an
// indefinite wait for the next real keyboard event, which becomes this
// chord's Alt modifier whenever it can be. There is deliberately no
// timeout - see EscapeSequenceCoalescer::escapePending()'s own comment for
// how a caller shows the waiting state without one.
//
// Two other notcurses legacy-encoding gaps get normalized here too, right
// alongside the Alt one - see normalizeLegacyAlt() and
// normalizeCtrlSpace() in EscapeCoalescer.cpp: notcurses's own ESC+letter
// merge only ever sets the deprecated `ncinput.alt` bool, never
// `modifiers`; and Ctrl-Space's legacy NUL-byte encoding is the one C0
// control byte notcurses's own load_ncinput() (in.c) doesn't convert into
// "letter plus NCKEY_MOD_CTRL" the way it does every other one.
//
// This header is notcurses-typed (ncinput, NCKEY_*) by necessity - the
// whole point is to disambiguate a raw notcurses encoding - and, like
// TerminalUI.h/.cpp's own use of ncinput, is meant to stay confined to the
// UI/terminal boundary layer; nothing below TerminalUI.cpp in this
// codebase should include it. It links against nothing: every notcurses
// symbol it touches (ncinput_alt_p(), NCKEY_ESC, NCKEY_MOD_ALT,
// ncintype_e, nckey_mouse_p()) is a macro, an enum, or a header-defined
// `static inline` function, so this file and its tests build and run with
// no notcurses library present at link time. NotcursesInputEventSource.h
// is the one piece of the mechanism that does need to link against
// notcurses (it calls notcurses_get()) - it stays in its own file for
// exactly that reason.

// Abstracts "return the next raw input event, if one is immediately
// available" so EscapeSequenceCoalescer can be driven from a real
// terminal or from a synthetic queue in tests. Always a nonblocking poll -
// see this header's own top comment on why no wait/deadline is needed
// here: an indefinite wait is implemented as state carried across
// separate next() calls (driven by the application's own event-loop
// polling), never as a blocking call inside one of them.
class InputEventSource {
public:
  virtual ~InputEventSource() = default;

  // Returns true and fills *ni if an event was immediately available;
  // false otherwise (nothing to report right now - try again later).
  // `definitely_pending`: the caller already has independent, external
  // confirmation that at least one real event is waiting right now (e.g.
  // notcurses's own input-ready fd just reported readiness via poll()) -
  // see NotcursesInputEventSource's own top comment for why this matters:
  // it's the only way to reliably tell a genuine codepoint-0 keystroke
  // (Ctrl-Space, the literal NUL byte) apart from "nothing is available
  // right now", which notcurses_get()'s nonblocking contract otherwise
  // reports identically. Implementations that have no such ambiguity to
  // resolve (every test fake in this codebase) are free to ignore it.
  virtual bool next(ncinput * ni, bool definitely_pending = false) = 0;
};

// Determines, from the shape of the events actually arriving this
// session, whether the terminal is running the Kitty keyboard
// disambiguation protocol - the same thing notcurses-info reports as its
// "kbd" property. There is no public notcurses API to query that
// capability directly: it's tracked internally as the private
// tinfo::kittykbdsupport field (confirmed against the installed notcurses
// headers/source), exposed only to notcurses's own in-tree notcurses-info
// tool via a header that isn't shipped with libnotcurses-dev. Instead,
// this relies on the same signal TerminalUI.cpp's readInput() already
// uses: a Kitty-protocol terminal reports real NCTYPE_PRESS/
// NCTYPE_REPEAT/NCTYPE_RELEASE for every keystroke, while a legacy
// terminal reports NCTYPE_UNKNOWN for all of them - and critically, this
// holds for the very first keystroke of the session, ESC included, so
// observing each event's evtype right before deciding whether to coalesce
// it is equivalent to a real startup query for every practical purpose
// (there is nothing to coalesce before the first keystroke arrives
// anyway).
class KittyProtocolDetector {
public:
  // Feeds one observed keyboard event's evtype into the detector.
  // Idempotent after the first observation - the verdict doesn't change
  // mid-session (a terminal doesn't renegotiate the protocol while
  // notcurses is running). Callers must only pass evtype from a genuine
  // keyboard event - SGR mouse press/release always carries a real
  // NCTYPE_PRESS/NCTYPE_RELEASE regardless of the keyboard protocol (see
  // EscapeSequenceCoalescer::next()'s own comment, which is why it
  // filters mouse events out via nckey_mouse_p() before calling this),
  // so feeding one through here would misdetect the protocol as active
  // from the session's first mouse click.
  void observe(ncintype_e evtype);

  bool active() const { return active_; }

private:
  bool active_ = false;
  bool settled_ = false;
};

// Coalesces a standalone NCKEY_ESC with a later Alt-chordable keystroke
// into a single Alt-modified ncinput - see this header's own top comment.
// This is the single input source the rest of the application should call
// instead of notcurses_get()/InputEventSource::next() directly whenever
// Alt-modified keys need to work uniformly across legacy and
// Kitty-protocol terminals.
class EscapeSequenceCoalescer {
public:
  explicit EscapeSequenceCoalescer(InputEventSource & source, KittyProtocolDetector & detector);

  // Suppresses coalescing entirely for as long as paste framing is open -
  // an ESC byte inside pasted text is data, not a keypress. Nothing in
  // this codebase currently calls this: the vendored notcurses build has
  // no bracketed-paste support of its own to drive it from (confirmed
  // against the installed headers/source - no NCKEY_PASTE, no bracketed-
  // paste enable sequence anywhere in notcurses's input layer, and this
  // codebase's own "paste" is only the internal Emacs-style yank
  // clipboard, C-y, never an OS-level paste), so a real paste containing
  // ESC is indistinguishable, today, from a genuine Escape keypress. The
  // hook exists, and is exercised by this component's own tests via the
  // injectable source, so a future paste-aware input layer has something
  // to wire into without touching this class again.
  void setInPaste(bool in_paste);

  // Fetches the next logical event, applying coalescing. Returns false if
  // none is available right now - mirrors the nonblocking-poll contract
  // callers already get from nc->get(false, ...)/notcurses_get_nblock().
  // Never blocks. `definitely_pending` is forwarded to the underlying
  // InputEventSource verbatim - see its own comment.
  bool next(ncinput * ni, bool definitely_pending = false);

  // True from the moment a bare Escape is seen until the wait it starts
  // resolves - either folded into the next keystroke's Alt modifier, or
  // given up on (a mouse/resize event doesn't count as a follow keystroke
  // and leaves this true; any other real keyboard event ends the wait,
  // successfully or not - see next()'s own comment). Callers use this to
  // show an Emacs-style "ESC-" indicator the whole time it's true; since
  // there's no deadline, that indicator is the only feedback a person
  // gets that the wait is still open, so it should be shown as soon as
  // this turns true, not deferred.
  bool escapePending() const { return pending_; }

private:
  InputEventSource & source_;
  KittyProtocolDetector & detector_;
  bool in_paste_ = false;
  bool pending_ = false;
};

#endif
