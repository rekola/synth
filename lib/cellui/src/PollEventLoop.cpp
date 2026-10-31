#include <cellui/PollEventLoop.h>

#include <poll.h>

namespace cellui {

void PollEventLoop::addSource(const std::vector<int> & fds, Handler handler) {
  sources_.push_back({ fds, std::move(handler) });
}

void PollEventLoop::addWriteSource(int fd, std::function<bool()> wanted, Handler handler) {
  write_sources_.push_back({ fd, std::move(wanted), std::move(handler) });
}

bool PollEventLoop::iterate(bool block) {
  int timeout = beforeTurn();
  if (!block) timeout = 0;
  std::vector<pollfd> polled;
  for (const Source & source : sources_) {
    for (int fd : source.fds) polled.push_back({ fd, POLLIN, 0 });
  }
  size_t read_count = polled.size();
  std::vector<size_t> writing;  // the write sources waited on
  for (size_t i = 0; i < write_sources_.size(); i++) {
    if (!write_sources_[i].wanted()) continue;
    writing.push_back(i);
    polled.push_back({ write_sources_[i].fd, POLLOUT, 0 });
  }
  bool polled_any = ::poll(polled.data(), polled.size(), timeout) > 0;
  // A write source becoming ready is not a wake for the application.
  bool any = false;
  for (size_t i = 0; polled_any && i < read_count; i++) any |= polled[i].revents != 0;
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
  for (size_t k = 0; k < writing.size(); k++) {
    if (!polled_any || !polled[read_count + k].revents) continue;
    Handler handler = write_sources_[writing[k]].handler;
    handler();
  }
  afterTurn(any);
  return any;
}

}  // namespace cellui
