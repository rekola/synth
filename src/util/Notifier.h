#ifndef _NOTIFIER_H_
#define _NOTIFIER_H_

#include <cstdint>

#ifndef _WIN32
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

// A level-triggered wakeup a thread blocks on: signal() raises it from any
// thread and it stays raised until clear(). A thread that waits on more than
// one thing puts it in a WaitSet.
class Notifier {
 public:
  Notifier(const Notifier &) = delete;
  Notifier & operator=(const Notifier &) = delete;

#ifdef _WIN32
  // A manual-reset event; defined in Notifier.cpp so <windows.h> stays out
  // of every header that includes this one.
  Notifier();
  ~Notifier();

  void signal();
  void clear();
  // Waits up to timeout_ms (negative: for as long as it takes); true if
  // raised. Does not clear.
  bool wait(int timeout_ms);

  void * handle() const { return handle_; }

 private:
  void * handle_;
#else
  Notifier() : fd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) { }
  ~Notifier() { close(fd_); }

  void signal() {
    uint64_t one = 1;
    (void)!write(fd_, &one, sizeof(one));
  }

  void clear() {
    uint64_t count;
    (void)!read(fd_, &count, sizeof(count));
  }

  // Waits up to timeout_ms (negative: for as long as it takes); true if
  // raised. Does not clear.
  bool wait(int timeout_ms) {
    pollfd d { fd_, POLLIN, 0 };
    return poll(&d, 1, timeout_ms) > 0 && d.revents;
  }

  int fd() const { return fd_; }

 private:
  int fd_;
#endif
};

#endif
