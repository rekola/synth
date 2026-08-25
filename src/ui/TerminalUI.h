#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"
#include "../playback/InputEvent.h"
#include <chrono>
#include <memory>
#include <optional>

namespace ncpp {
  class NotCurses;
};

class TerminalUI : public UI {
 public:
  explicit TerminalUI(std::shared_ptr<ncpp::NotCurses> _nc);
  ~TerminalUI();

  void initialize(std::shared_ptr<Controller> & controller);

  void refresh() override;
  void render() override;

protected:
  void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) override;
  bool readInput();

  // How long a pending Escape (EscapeSequenceCoalescer::escapePending())
  // must stay open before startUI()'s own loop shows the "ESC-" indicator
  // - see that loop's own comment on why this is a delay rather than
  // immediate, and updateEscapeIndicator()'s comment for what drives it.
  int escapeIndicatorPollTimeoutMs() const;
  void updateEscapeIndicator();

private:
  std::shared_ptr<ncpp::NotCurses> nc;

  // Legacy xterm/VT220 "SS3 + modifier digit" escape sequence recognizer
  // for Ctrl+Numpad-Divide/Multiply (see readInput()'s own comment) - the
  // raw bytes buffered so far (ESC, then 'O', then '5') can be replayed
  // verbatim as ordinary InputEvents if what looked like the start of the
  // sequence turns out not to be (e.g. a real standalone Escape, or
  // Esc-then-x for M-x). Kept notcurses-type-free (plain ints/unsigned
  // rather than ncinput) so this header doesn't need a notcurses include.
  struct PendingRawKey { int id = 0, y = 0, x = 0; unsigned modifiers = 0; InputEvent::Kind kind = InputEvent::Kind::UNKNOWN; };
  int kp_escape_depth_ = 0; // 0 = no legacy KP-escape sequence in progress, 1-3 = that many bytes matched so far
  PendingRawKey kp_escape_pending_[3]; // ESC, 'O', '5', in order

  // Alt-key coalescing (EscapeCoalescer.h) - the notcurses input source
  // readInput() actually pulls events from. Kept behind a pointer to an
  // incomplete type, defined only in TerminalUI.cpp, for the same reason
  // kp_escape_pending_ above is kept plain-int typed: this header
  // shouldn't need a notcurses include.
  class EscapeInputPipeline;
  std::unique_ptr<EscapeInputPipeline> escape_input_;

  // When the currently-open Escape wait began (unset when none is open) -
  // set in readInput() the moment EscapeSequenceCoalescer::escapePending()
  // turns true, read by updateEscapeIndicator()/escapeIndicatorPollTimeoutMs()
  // to decide when the delay has elapsed. escape_indicator_shown_ tracks
  // whether "ESC-" has actually been printed yet for the current wait, so
  // it's only cleared (and only if it was actually shown) once, exactly
  // when the wait ends.
  std::optional<std::chrono::steady_clock::time_point> escape_pending_since_;
  bool escape_indicator_shown_ = false;
};

#endif
