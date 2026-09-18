#ifndef _EVENTQUEUE_H_
#define _EVENTQUEUE_H_

#include "Event.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <deque>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

// Unbounded multi-producer/single-consumer queue with a pollable fd. The
// deque is the only source of truth; the eventfd is just a wakeup signal,
// kept readable exactly while the deque is non-empty (both updated under
// the same lock). An eventfd write never blocks, so push() can't stall
// the producer however far behind the consumer falls.
class EventQueue {
 public:
  EventQueue() : event_fd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) { }
  ~EventQueue() { close(event_fd); }

  EventQueue(const EventQueue &) = delete;
  EventQueue & operator=(const EventQueue &) = delete;

  void push(std::unique_ptr<Event> event) {
    std::lock_guard<std::mutex> guard(event_mutex);
    events.push_front(std::move(event));
    uint64_t one = 1;
    (void)!write(event_fd, &one, sizeof(one));
  }

  int getPollFd() const { return event_fd; }

  // Blocks until an event is available.
  std::unique_ptr<Event> pop() {
    while (true) {
      {
        std::lock_guard<std::mutex> guard(event_mutex);
        if (!events.empty()) {
          auto e = std::move(events.back());
          events.pop_back();
          if (events.empty()) {
            uint64_t count;
            (void)!read(event_fd, &count, sizeof(count));
          }
          return e;
        }
      }
      pollfd d { event_fd, POLLIN, 0 };
      poll(&d, 1, -1);
    }
  }

  bool hasEvents() const {
    std::lock_guard<std::mutex> guard(event_mutex);
    return !events.empty();
  }

 private:
  int event_fd;
  mutable std::mutex event_mutex;
  std::deque<std::unique_ptr<Event> > events;
};

#endif
