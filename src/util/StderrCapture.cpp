#include "StderrCapture.h"

#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <mutex>
#include <thread>

struct StderrCapture::Impl {
  Notifier notifier;
  HANDLE read_end = nullptr, write_end = nullptr;
  int saved_fd = -1;  // the caller's stderr, when it is not a console
  std::mutex mutex;
  std::string pending;
  std::thread reader;
  bool active = false;

  void run() {
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(read_end, buffer, sizeof buffer, &got, nullptr) && got > 0) {
      if (saved_fd >= 0) {
        // Best effort: a failed write must not take the UI down.
        DWORD written = 0;
        while (written < got) {
          int n = _write(saved_fd, buffer + written, got - written);
          if (n <= 0) break;
          written += static_cast<DWORD>(n);
        }
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        pending.append(buffer, got);
      }
      notifier.signal();
    }
  }
};

StderrCapture::StderrCapture() : impl_(new Impl) {
  Impl & i = *impl_;
  if (!CreatePipe(&i.read_end, &i.write_end, nullptr, 0)) return;
  int write_fd = _open_osfhandle(reinterpret_cast<intptr_t>(i.write_end), _O_WRONLY | _O_BINARY);
  if (write_fd < 0) {
    CloseHandle(i.read_end);
    CloseHandle(i.write_end);
    i.read_end = i.write_end = nullptr;
    return;
  }
  fflush(stderr);
  if (!_isatty(2)) i.saved_fd = _dup(2);
  _dup2(write_fd, 2);
  _close(write_fd);  // closes the handle too; descriptor 2 keeps its own
  i.write_end = nullptr;
  setvbuf(stderr, nullptr, _IONBF, 0);
  i.active = true;
  i.reader = std::thread([this] { impl_->run(); });
}

StderrCapture::~StderrCapture() {
  Impl & i = *impl_;
  if (!i.active) return;
  // Put stderr back; the pipe's write side closes with descriptor 2's copy,
  // which ends the reader.
  fflush(stderr);
  if (i.saved_fd >= 0) {
    _dup2(i.saved_fd, 2);
    _close(i.saved_fd);
  } else {
    int nul = _open("NUL", _O_WRONLY);
    if (nul >= 0) {
      _dup2(nul, 2);
      _close(nul);
    }
  }
  if (i.reader.joinable()) i.reader.join();
  CloseHandle(i.read_end);
}

bool StderrCapture::active() const { return impl_->active; }
Notifier & StderrCapture::notifier() { return impl_->notifier; }

std::string StderrCapture::take() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->notifier.clear();
  std::string out;
  out.swap(impl_->pending);
  return out;
}
