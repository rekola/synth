#pragma once

#include <cellui/EventLoop.h>

namespace cellui {

// An EventLoop that waits with poll(2).
class PollEventLoop : public EventLoop {
 public:
  PollEventLoop() { installWake(); }
  void addSource(const std::vector<int> & fds, Handler handler) override;
  bool iterate(bool block) override;

 private:
  struct Source {
    std::vector<int> fds;
    Handler handler;
  };
  std::vector<Source> sources_;
};

}  // namespace cellui
