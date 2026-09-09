#ifndef _NOTCURSESINPUTEVENTSOURCE_H_
#define _NOTCURSESINPUTEVENTSOURCE_H_

#include "EscapeCoalescer.h"

namespace ncpp {
  class NotCurses;
};

// The real InputEventSource, wrapping ncpp::NotCurses::get() (and so,
// transitively, notcurses_get()) - the one piece of the Alt-key
// coalescing mechanism that needs to link against notcurses;
// EscapeCoalescer.h/.cpp deliberately don't, so they can be unit-tested
// with no notcurses library present at link time (see EscapeCoalescer.h's
// own top comment).
//
// Also where `definitely_pending` (InputEventSource::next()'s own
// comment) actually matters: notcurses_get()'s nonblocking contract
// returns 0 both for "no input available" and for a genuine ncinput whose
// codepoint is 0 - Ctrl-Space, the legacy encoding for which is a literal
// NUL byte (confirmed against the installed 3.0.17 source: every return
// path of internal_get() in in.c, in.c:2776's notcurses_get(), either
// memcpy()s a fully-populated, already-decoded event out of its ring
// buffer, or memset()s *ni to all-zero on a genuine timeout/EOF - both
// produce an ncinput with id 0, evtype NCTYPE_UNKNOWN, modifiers 0, byte-
// for-byte identical, so nothing in the returned struct can tell them
// apart). See next()'s own comment for how `definitely_pending` resolves
// this.
class NotcursesInputEventSource : public InputEventSource {
public:
  explicit NotcursesInputEventSource(ncpp::NotCurses & nc) : nc_(nc) { }

  bool next(ncinput * ni, bool definitely_pending = false) override;

private:
  ncpp::NotCurses & nc_;
};

#endif
