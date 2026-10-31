#pragma once

#include <cellui/EventLoop.h>

namespace cellui {

// An EventLoop that waits with poll(2).
class PollEventLoop : public EventLoop {
 public:
  PollEventLoop() { installWake(); }
  void addSource(const std::vector<int> & fds, Handler handler) override;
  bool iterate(bool block) override;

  // `handler` runs, in a turn, when `fd` can take a write. The descriptor is
  // only waited on for writing while `wanted()` is true (checked at the
  // start of each wait), so a source with nothing to write costs no wakeups.
  void addWriteSource(int fd, std::function<bool()> wanted, Handler handler);

 private:
  struct Source {
    std::vector<int> fds;
    Handler handler;
  };
  std::vector<Source> sources_;
  struct WriteSource {
    int fd;
    std::function<bool()> wanted;
    Handler handler;
  };
  std::vector<WriteSource> write_sources_;
};

}  // namespace cellui
