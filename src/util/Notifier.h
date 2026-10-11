#ifndef _NOTIFIER_H_
#define _NOTIFIER_H_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#ifndef _WIN32
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

// A level-triggered wakeup a thread blocks on: signal() raises it from any
// thread and it stays raised until clear(). Where a waiter also needs other
// descriptors (the UI loops, ALSA), fd() is what it polls.
class Notifier {
 public:
#ifdef _WIN32
  Notifier() = default;

  void signal() {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      raised_ = true;
    }
    cv_.notify_all();
  }

  void clear() {
    std::lock_guard<std::mutex> guard(mutex_);
    raised_ = false;
  }

  // Waits up to timeout_ms (negative: for as long as it takes); true if
  // raised. Does not clear.
  bool wait(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms < 0) {
      cv_.wait(lock, [this] { return raised_; });
      return true;
    }
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return raised_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool raised_ = false;
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

 public:
  Notifier(const Notifier &) = delete;
  Notifier & operator=(const Notifier &) = delete;
};

#endif
