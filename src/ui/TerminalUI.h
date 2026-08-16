#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"
#include "../playback/InputEvent.h"
#include <memory>

namespace ncpp {
  class NotCurses;
};

class TerminalUI : public UI {
 public:
  explicit TerminalUI(std::shared_ptr<ncpp::NotCurses> _nc) : nc(_nc) { }
  ~TerminalUI() { }

  void initialize(std::shared_ptr<Controller> & controller);

  void refresh() override;
  void render() override;

protected:
  void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) override;
  bool readInput();

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
};

#endif
