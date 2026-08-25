#include "NotcursesInputEventSource.h"

#include <ncpp/NotCurses.hh>

bool
NotcursesInputEventSource::next(ncinput * ni) {
  // notcurses_get_nblock()'s own contract: 0 for "no input available",
  // (uint32_t)-1 on EOF/error, otherwise a successful read - both
  // non-success cases compare unequal to a successful read the same way
  // TerminalUI.cpp's own drain loop already tested it before this class
  // existed (`> 0` on the same unsigned return), so this mirrors existing
  // behavior rather than introducing separate EOF handling.
  return nc_.get(false, ni) != 0;
}
