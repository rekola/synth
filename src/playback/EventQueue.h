#ifndef _EVENTQUEUE_H_
#define _EVENTQUEUE_H_

#include "Event.h"
#include "../util/Notifier.h"

#include <deque>
#include <memory>
#include <mutex>

// Unbounded multi-producer/single-consumer queue with a wakeup a consumer can
// wait on. The deque is the only source of truth; the notifier is just a
// signal, kept raised exactly while the deque is non-empty (both updated
// under the same lock). Raising it never blocks, so push() can't stall the
// producer however far behind the consumer falls.
class EventQueue {
 public:
  EventQueue() = default;

  EventQueue(const EventQueue &) = delete;
  EventQueue & operator=(const EventQueue &) = delete;

  void push(std::unique_ptr<Event> event) {
    std::lock_guard<std::mutex> guard(event_mutex);
    events.push_front(std::move(event));
    notifier_.signal();
  }

  // What a consumer that waits on more than this queue passes along.
  Notifier & notifier() { return notifier_; }
#ifndef _WIN32
  int getPollFd() const { return notifier_.fd(); }
#endif

  // Waits up to timeout_ms for an event; true if one is available.
  bool wait(int timeout_ms) { return notifier_.wait(timeout_ms); }

  // Blocks until an event is available.
  std::unique_ptr<Event> pop() {
    while (true) {
      {
        std::lock_guard<std::mutex> guard(event_mutex);
        if (!events.empty()) {
          auto e = std::move(events.back());
          events.pop_back();
          if (events.empty()) notifier_.clear();
          return e;
        }
      }
      notifier_.wait(-1);
    }
  }

  bool hasEvents() const {
    std::lock_guard<std::mutex> guard(event_mutex);
    return !events.empty();
  }

 private:
  Notifier notifier_;
  mutable std::mutex event_mutex;
  std::deque<std::unique_ptr<Event> > events;
};

#endif
