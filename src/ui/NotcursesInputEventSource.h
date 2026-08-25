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
class NotcursesInputEventSource : public InputEventSource {
public:
  explicit NotcursesInputEventSource(ncpp::NotCurses & nc) : nc_(nc) { }

  bool next(ncinput * ni) override;

private:
  ncpp::NotCurses & nc_;
};

#endif
