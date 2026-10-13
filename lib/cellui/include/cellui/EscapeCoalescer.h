#pragma once

#include <cellui/Input.h>

namespace cellui {

// Coalesces the legacy Meta/Alt-key encoding - a plain Escape followed by
// the modified key, with no guarantee they arrive close together in time -
// into a single Alt-modified input, matching what a terminal running the
// kitty keyboard protocol sends directly. Modeled on Emacs's own
// ESC-as-Meta-prefix behavior (pressing Escape, waiting indefinitely -
// seconds, not milliseconds - then pressing a key still produces the
// Meta-modified command; Emacs shows "ESC-" in the echo area the whole time
// it's waiting) rather than on a short timing window: a bare Escape arms an
// indefinite wait for the next real keyboard event, which becomes this
// chord's Alt modifier whenever it can be. There is deliberately no timeout
// - see EscapeSequenceCoalescer::escapePending()'s own comment for how a
// caller shows the waiting state without one. (The terminal backend's own
// parser already folds an Escape and a key that arrive together, and
// delivers a lone Escape after a short grace; this is the wait beyond it.)

// Abstracts "return the next input, if one is immediately available" so
// EscapeSequenceCoalescer can be driven from a real terminal or from a
// synthetic queue in tests. Always a nonblocking poll: an indefinite wait is
// state carried across separate next() calls, never a blocking call.
class InputEventSource {
public:
  virtual ~InputEventSource() = default;

  // Returns true and fills *input if one was immediately available.
  virtual bool next(Input * input) = 0;
};

// Determines, from the events arriving this session, whether the terminal
// runs the kitty keyboard protocol: a kitty-protocol terminal reports a
// real press/repeat/release for every keystroke, a legacy terminal UNKNOWN
// for all of them - from the session's very first keystroke, Escape
// included, so observing each event right before deciding whether to
// coalesce it is as good as asking.
class KittyProtocolDetector {
public:
  // Feeds one keyboard event's kind into the detector. Idempotent after the
  // first observation. Callers pass only genuine keyboard events - a mouse
  // press/release always carries a real kind regardless of the keyboard
  // protocol, so feeding one would misdetect the protocol as active from
  // the session's first click.
  void observe(Input::Kind kind);

  bool active() const { return active_; }

private:
  bool active_ = false;
  bool settled_ = false;
};

// Coalesces a standalone Escape with a later Alt-chordable keystroke into a
// single Alt-modified input - see this header's top comment.
class EscapeSequenceCoalescer {
public:
  explicit EscapeSequenceCoalescer(InputEventSource & source, KittyProtocolDetector & detector);

  // Suppresses coalescing while pasted text is being delivered as keys - an
  // ESC in pasted text is data, not a keypress.
  void setInPaste(bool in_paste);

  // Fetches the next logical input, applying coalescing. Returns false if
  // none is available right now. Never blocks.
  bool next(Input * input);

  // True from the moment a bare Escape is seen until the wait it starts
  // resolves - folded into the next keystroke's Alt modifier, or given up
  // on (a mouse/resize event doesn't count as a follow keystroke and leaves
  // this true; any other real keyboard event ends the wait, successfully or
  // not). Callers use this to show an Emacs-style "ESC-" indicator.
  bool escapePending() const { return pending_; }

private:
  InputEventSource & source_;
  KittyProtocolDetector & detector_;
  bool in_paste_ = false;
  bool pending_ = false;
};

}  // namespace cellui
