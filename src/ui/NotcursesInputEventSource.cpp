#include "NotcursesInputEventSource.h"

#include <ncpp/NotCurses.hh>

namespace {

// Purely a defensive cap, not a wait this call is ever expected to
// actually block for - see next()'s own comment. Kept short so the one
// scenario where the reasoning behind `definitely_pending` turns out to
// have a gap this hasn't anticipated costs a barely-perceptible stall
// instead of a frozen UI.
constexpr long kDefinitelyPendingBoundNs = 10'000'000; // 10ms

}

bool
NotcursesInputEventSource::next(ncinput * ni, bool definitely_pending) {
  if (!definitely_pending) {
    // notcurses_get_nblock()'s own contract: 0 for "no input available",
    // (uint32_t)-1 on EOF/error, otherwise a successful read - both
    // non-success cases compare unequal to a successful read the same way
    // TerminalUI.cpp's own drain loop already tested it before this class
    // existed (`> 0` on the same unsigned return), so this mirrors
    // existing behavior rather than introducing separate EOF handling.
    // Ambiguous for a genuine codepoint-0 event (Ctrl-Space) - see this
    // class's own top comment and the `definitely_pending` branch below.
    return nc_.get(false, ni) != 0;
  }

  // The caller (TerminalUI::readInput(), for the first dequeue of a
  // batch) already has independent confirmation - via poll() reporting
  // notcurses's own get_inputready_fd() readable - that a real event is
  // sitting in notcurses's internal queue right now. That fd is written
  // to (mark_pipe_ready() in in.c) from exactly two places: after a
  // genuine decoded event is already committed to the queue, or on an
  // EOF transition (which returns the distinct NCKEY_EOF sentinel, never
  // 0) - so a 0 return from *this* call cannot be a real "nothing here"
  // timeout the way it can for an ordinary nonblocking poll: the queue
  // isn't empty, so notcurses_get() takes its immediate-dequeue path
  // rather than ever actually waiting out the bound below. A bounded (not
  // literal 0, and not indefinite either) deadline is used anyway, purely
  // as a defensive cap in case that reasoning has a gap this hasn't
  // anticipated - if so, this returns "no event" once, the same as
  // today's behavior, rather than hanging the UI thread waiting for a
  // keystroke that may never come.
  struct timespec ts = { 0, kDefinitelyPendingBoundNs };
  nc_.get(&ts, ni);
  return true;
}
