#ifndef _EVENTQUEUE_H_
#define _EVENTQUEUE_H_

#include "Event.h"

#include <cassert>
#include <mutex>
#include <deque>
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>

class EventQueue {
 public:
  EventQueue() {
    int fd[2];
    socketpair(PF_LOCAL, SOCK_STREAM, 0, fd);
    
    poll_fd = fd[0];
    push_fd = fd[1];
  }
  ~EventQueue() {
    close(poll_fd);
    close(push_fd);
  }

  void push(std::unique_ptr<Event> event) {
    std::lock_guard<std::mutex> guard(event_mutex);
    events.push_front(std::move(event));
    uint8_t a = 0;
    if (write(push_fd, &a, sizeof(uint8_t)) != sizeof(uint8_t)) {
      throw std::runtime_error("unable to write to queue");
    }
  }

  int getPollFd() const { return poll_fd; }
  
  std::unique_ptr<Event> pop() {
    while (!hasEvents()) {
      unsigned char buffer[4096];
      auto s = read(poll_fd, buffer, 4096);
      if (s >= 0) {
	pending_event_count += s;
      }
    }

    assert(pending_event_count);
    
    std::lock_guard<std::mutex> guard(event_mutex);
    auto e = std::move(events.back());
    events.pop_back();
    pending_event_count--;
    return e;
  }

  bool hasEvents() const { return pending_event_count > 0; }

 private:
  int poll_fd, push_fd;
  std::mutex event_mutex;
  std::deque<std::unique_ptr<Event> > events;
  int pending_event_count = 0;
};

#endif
