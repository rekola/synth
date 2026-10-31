#include <cellui/term/OutputQueue.h>

#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <unistd.h>

namespace cellui::term {

bool OutputQueue::flush(int fd) {
  if (empty() || fd < 0) return true;
  sigset_t block, old;
  sigemptyset(&block);
  // Parenthesised to call the function: where sigaddset is a macro (macOS) it
  // does int arithmetic on an unsigned sigset_t.
  for (int s : { SIGINT, SIGTERM, SIGHUP, SIGQUIT }) (sigaddset)(&block, s);
  pthread_sigmask(SIG_BLOCK, &block, &old);
  bool ok = true;
  while (!empty()) {
    ssize_t n = ::write(fd, buffer_.data() + offset_, buffer_.size() - offset_);
    if (n > 0) {
      offset_ += static_cast<size_t>(n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else if (n < 0 && errno == EAGAIN) {
      break;
    } else {
      ok = false;
      break;
    }
  }
  pthread_sigmask(SIG_SETMASK, &old, nullptr);
  if (empty()) {
    buffer_.clear();
    offset_ = 0;
  } else if (offset_ > (1u << 16)) {
    buffer_.erase(0, offset_);
    offset_ = 0;
  }
  return ok;
}

}  // namespace cellui::term
