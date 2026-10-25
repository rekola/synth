#ifndef _WAITSOURCES_H_
#define _WAITSOURCES_H_

#include "../util/WaitSet.h"

#include <cellui/EventLoop.h>

#include <utility>

// Makes a WaitSet group a source of `loop`: descriptors on POSIX, handles on
// Windows. `handler` runs in a turn when any of them is ready, and must read
// what made it ready.
inline void
addWaitGroup(cellui::EventLoop & loop, const WaitSet & waits, WaitSet::Group group, cellui::EventLoop::Handler handler) {
#ifdef _WIN32
  loop.addHandles(waits.handles(group), std::move(handler));
#else
  loop.addSource(waits.fds(group), std::move(handler));
#endif
}

#endif
