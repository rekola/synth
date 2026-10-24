#pragma once

namespace cellui {

// A level-triggered signal any thread may raise and a loop waits on: an
// eventfd on Linux, a non-blocking pipe on other POSIX systems, an event
// handle on Windows.
class Wakeup {
 public:
  Wakeup();
  ~Wakeup();
  Wakeup(const Wakeup &) = delete;
  Wakeup & operator=(const Wakeup &) = delete;

  // Makes the descriptor (handle) ready. Any thread; allocates and blocks
  // on nothing.
  void signal() noexcept;
  // Clears it. The waiting thread, after it woke.
  void drain() noexcept;

  // What a POSIX loop waits on, -1 on Windows.
  int fd() const { return fd_; }
  // What a Windows loop waits on, null elsewhere.
  void * handle() const { return handle_; }

 private:
  int fd_ = -1;
  int write_fd_ = -1;  // the pipe's write end; the eventfd is both
  void * handle_ = nullptr;
};

}  // namespace cellui
