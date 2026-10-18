#ifndef _NOTIFIER_H_
#define _NOTIFIER_H_

#include <cstdint>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/eventfd.h>
#endif
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
#elif defined(__linux__)
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
#else
  // No eventfd: a non-blocking pipe. Raised while it holds a byte; a full
  // pipe is already raised, so a failed write is fine.
  Notifier() {
    if (pipe(fds_) != 0) fds_[0] = fds_[1] = -1;
    for (int fd : fds_) {
      if (fd < 0) continue;
      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
      fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
  }
  ~Notifier() {
    close(fds_[0]);
    close(fds_[1]);
  }

  void signal() {
    char one = 1;
    (void)!write(fds_[1], &one, 1);
  }

  void clear() {
    char buf[64];
    while (read(fds_[0], buf, sizeof(buf)) > 0) { }
  }

  // Waits up to timeout_ms (negative: for as long as it takes); true if
  // raised. Does not clear.
  bool wait(int timeout_ms) {
    pollfd d { fds_[0], POLLIN, 0 };
    return poll(&d, 1, timeout_ms) > 0 && d.revents;
  }

  int fd() const { return fds_[0]; }

 private:
  int fds_[2];
#endif
};

#endif
