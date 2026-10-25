#include <cellui/PollEventLoop.h>

#include <poll.h>

namespace cellui {

void PollEventLoop::addSource(const std::vector<int> & fds, Handler handler) {
  sources_.push_back({ fds, std::move(handler) });
}

bool PollEventLoop::iterate(bool block) {
  int timeout = beforeTurn();
  if (!block) timeout = 0;
  std::vector<pollfd> polled;
  for (const Source & source : sources_) {
    for (int fd : source.fds) polled.push_back({ fd, POLLIN, 0 });
  }
  bool any = ::poll(polled.data(), polled.size(), timeout) > 0;
  // Which sources are ready is settled before any handler runs, which may
  // add sources.
  std::vector<char> ready(sources_.size(), 0);
  if (any) {
    size_t at = 0;
    for (size_t i = 0; i < sources_.size(); i++) {
      for (size_t k = 0; k < sources_[i].fds.size(); k++, at++) {
        if (polled[at].revents) ready[i] = 1;
      }
    }
  }
  for (size_t i = 0; i < ready.size(); i++) {
    if (!ready[i]) continue;
    // Copied: a handler may add a source, moving the vector.
    Handler handler = sources_[i].handler;
    handler();
  }
  afterTurn(any);
  return any;
}

}  // namespace cellui
