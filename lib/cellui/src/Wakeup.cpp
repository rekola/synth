#include <cellui/Wakeup.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/eventfd.h>
#endif
#endif

#include <cstdint>

namespace cellui {

#ifdef _WIN32

Wakeup::Wakeup() : handle_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) { }
Wakeup::~Wakeup() {
  if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
}
void Wakeup::signal() noexcept { SetEvent(static_cast<HANDLE>(handle_)); }
void Wakeup::drain() noexcept { ResetEvent(static_cast<HANDLE>(handle_)); }

#else

Wakeup::Wakeup() {
#ifdef __linux__
  fd_ = write_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#else
  int fds[2];
  if (pipe(fds) == 0) {
    for (int fd : fds) {
      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
      fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    fd_ = fds[0];
    write_fd_ = fds[1];
  }
#endif
}

Wakeup::~Wakeup() {
  if (write_fd_ >= 0 && write_fd_ != fd_) ::close(write_fd_);
  if (fd_ >= 0) ::close(fd_);
}

void Wakeup::signal() noexcept {
#ifdef __linux__
  uint64_t one = 1;
  // A full counter already says "ready".
  [[maybe_unused]] ssize_t n = ::write(write_fd_, &one, sizeof one);
#else
  char byte = 1;
  // A full pipe already says "ready".
  [[maybe_unused]] ssize_t n = ::write(write_fd_, &byte, 1);
#endif
}

void Wakeup::drain() noexcept {
#ifdef __linux__
  uint64_t count;
  [[maybe_unused]] ssize_t n = ::read(fd_, &count, sizeof count);
#else
  char buffer[64];
  while (::read(fd_, buffer, sizeof buffer) > 0) { }
#endif
}

#endif

}  // namespace cellui
