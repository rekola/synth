#include "Notifier.h"

#ifdef _WIN32
#include <windows.h>

Notifier::Notifier() : handle_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) { }

Notifier::~Notifier() {
  CloseHandle(handle_);
}

void
Notifier::signal() {
  SetEvent(handle_);
}

void
Notifier::clear() {
  ResetEvent(handle_);
}

bool
Notifier::wait(int timeout_ms) {
  return WaitForSingleObject(handle_, timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms)) == WAIT_OBJECT_0;
}
#endif
