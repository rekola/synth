#include <cellui/EventLoop.h>

#include <algorithm>

namespace cellui {

void EventLoop::installWake() {
#ifdef _WIN32
  addHandles({ wakeup_.handle() }, [this] { woke(); });
#else
  addSource({ wakeup_.fd() }, [this] { woke(); });
#endif
}

void EventLoop::woke() {
  // Cleared first: a post after this raises it again, for the next turn.
  wakeup_.drain();
  woken_.emit();
}

void EventLoop::run(const void * opener) {
  stops_.push_back(0);
  openers_.push_back(opener);
  size_t level = stops_.size();
  while (!stops_[level - 1]) iterate(true);
  stops_.pop_back();
  openers_.pop_back();
  // What was given up in the run is only still wanted by the turn that
  // opened it.
  releaseDeferred(static_cast<int>(level));
}

void EventLoop::quit() {
  if (!stops_.empty()) stops_.back() = 1;
}

void EventLoop::defer(std::shared_ptr<void> object, const void * identity) {
  size_t level = stops_.size();
  if (level == 0) return;  // no turn to wait for: destroyed on return
  if (deferred_.size() <= level) deferred_.resize(level + 1);
  deferred_[level].push_back({ std::move(object), identity });
}

void EventLoop::releaseDeferred(int level) {
  if (level < 0 || static_cast<size_t>(level) >= deferred_.size()) return;
  // Taken out first: a destructor may defer more, or run a loop.
  std::vector<Deferred> items = std::move(deferred_[static_cast<size_t>(level)]);
  deferred_[static_cast<size_t>(level)].clear();
  std::vector<Deferred> kept;
  for (Deferred & item : items) {
    bool opener = item.identity && std::find(openers_.begin(), openers_.end(), item.identity) != openers_.end();
    if (opener) kept.push_back(std::move(item));
    else item.object.reset();
  }
  if (kept.empty()) return;
  // Out a level, to be seen by the turn that called the run() it opened.
  size_t out = level > 0 ? static_cast<size_t>(level) - 1 : 0;
  if (deferred_.size() <= out) deferred_.resize(out + 1);
  for (Deferred & item : kept) deferred_[out].push_back(std::move(item));
}

}  // namespace cellui
