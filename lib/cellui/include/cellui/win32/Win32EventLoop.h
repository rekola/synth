#pragma once

#include <cellui/EventLoop.h>

namespace cellui::win32 {

// An EventLoop on the thread's message queue: a turn waits with
// MsgWaitForMultipleObjectsEx for a source's handle or a window message,
// dispatches the messages, then runs the handlers of the sources that are
// signalled. Windows has no descriptors to poll, so a source is a list of
// waitable handles (events, in practice) added with addHandles().
class Win32EventLoop : public EventLoop {
 public:
  // Descriptors do not exist here: this does nothing. Use addHandles().
  void addSource(const std::vector<int> &, Handler) override { }

  // `handler` runs, in a turn, when any of `handles` (HANDLEs) is signalled.
  // Like the descriptors of other loops they are level-triggered: the
  // handler must reset what it reads. At most 63 handles in all.
  void addHandles(const std::vector<void *> & handles, Handler handler) override;

  bool iterate(bool block) override;

 private:
  struct Source {
    std::vector<void *> handles;
    Handler handler;
  };
  std::vector<Source> sources_;
};

}  // namespace cellui::win32
