#pragma once

#include <cellui/Input.h>

namespace cellui {

// Coalesces the legacy Meta/Alt-key encoding - a plain Escape followed by
// the modified key, with no guarantee they arrive close together in time -
// into a single Alt-modified input, the same input as a physical Alt chord.
// It applies under every keyboard protocol: a terminal that reports Alt
// chords directly still sends Escape as a key of its own, and Escape then a
// key is Meta in Emacs there too. Modeled on Emacs's own
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

// Coalesces a standalone Escape with a later Alt-chordable keystroke into a
// single Alt-modified input - see this header's top comment.
class EscapeSequenceCoalescer {
public:
  explicit EscapeSequenceCoalescer(InputEventSource & source);

  // Suppresses coalescing while pasted text is being delivered as keys - an
  // ESC in pasted text is data, not a keypress.
  void setInPaste(bool in_paste);

  // Fetches the next logical input, applying coalescing. Returns false if
  // none is available right now. Never blocks.
  bool next(Input * input);

  // True from the moment a bare Escape is seen until the wait it starts
  // resolves - folded into the next keystroke's Alt modifier, or given up
  // on (a mouse/resize event or a key release doesn't count as a follow
  // keystroke and leaves this true; any other real keyboard event ends the
  // wait, successfully or not). Callers use this to show an Emacs-style "ESC-" indicator.
  bool escapePending() const { return pending_; }
  // Whether the input next() returned last was folded into an Alt chord
  // (its press; the key's release carries the Alt too).
  bool lastFolded() const { return last_folded_; }

private:
  InputEventSource & source_;
  bool in_paste_ = false;
  bool pending_ = false;
  bool last_folded_ = false;
  int folded_id_ = -1;  // the key whose press was folded, until its release
};

}  // namespace cellui
