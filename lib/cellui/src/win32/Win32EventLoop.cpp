#include <cellui/win32/Win32EventLoop.h>

#include <windows.h>

namespace cellui::win32 {

void Win32EventLoop::addHandles(const std::vector<void *> & handles, Handler handler) {
  sources_.push_back({ handles, std::move(handler) });
}

bool Win32EventLoop::iterate(bool block) {
  int timeout = beforeTurn();
  if (!block) timeout = 0;

  std::vector<HANDLE> waited;
  for (const Source & source : sources_) {
    for (void * h : source.handles) {
      if (waited.size() < MAXIMUM_WAIT_OBJECTS - 1) waited.push_back(h);
    }
  }
  // MWMO_INPUTAVAILABLE: a message already in the queue ends the wait even
  // if it was seen by an earlier call.
  MsgWaitForMultipleObjectsEx(static_cast<DWORD>(waited.size()), waited.empty() ? nullptr : waited.data(),
                              timeout < 0 ? INFINITE : static_cast<DWORD>(timeout), QS_ALLINPUT, MWMO_INPUTAVAILABLE);

  // The window procedures run here and may signal the sources.
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  // Which sources are ready is settled before any handler runs, which may
  // add sources.
  std::vector<char> ready(sources_.size(), 0);
  bool any = false;
  for (size_t i = 0; i < sources_.size(); i++) {
    for (void * h : sources_[i].handles) {
      if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) {
        ready[i] = 1;
        any = true;
        break;
      }
    }
  }
  for (size_t i = 0; i < ready.size(); i++) {
    if (!ready[i]) continue;
    // Copied: a handler may add a source, moving the vector.
    Handler handler = sources_[i].handler;
    handler();
  }
  afterTurn(any);
  return any;
}

}  // namespace cellui::win32
