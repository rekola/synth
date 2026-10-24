#include <cellui/ThreadPosts.h>

namespace cellui {

void ThreadInbox::drain() {
  // One event at a time, so a handler that runs a nested loop (which drains
  // again) takes the next one in order.
  size_t waiting = size_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < waiting; i++) {
    std::unique_ptr<Posted> entry;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.empty()) break;
      entry = std::move(queue_.front());
      queue_.pop_front();
      size_.store(queue_.size(), std::memory_order_relaxed);
    }
    entry->dispatch(root_);
  }
  // A ring added by a handler is seen from the next turn.
  for (size_t r = 0; r < rings_.size(); r++) {
    RealtimeRing & ring = *rings_[r];
    for (size_t n = ring.available(); n > 0; n--) {
      RealtimeRing::Slot slot;
      if (!ring.take(slot)) break;
      slot.dispatch(root_, slot.bytes);
    }
  }
}

}  // namespace cellui
