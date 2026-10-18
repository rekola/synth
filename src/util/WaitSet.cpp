#include "WaitSet.h"

#ifdef _WIN32
#include <windows.h>
#endif

void
WaitSet::add(Group group, Notifier & notifier) {
#ifdef _WIN32
  slots_.push_back({ notifier.handle(), group });
#else
  addFd(group, notifier.fd());
#endif
}

#ifndef _WIN32
void
WaitSet::addFd(Group group, int fd) {
  slots_.push_back({ fd, group });
}

std::vector<int>
WaitSet::fds(Group group) const {
  std::vector<int> result;
  for (auto & slot : slots_) {
    if (slot.group == group) result.push_back(slot.fd);
  }
  return result;
}

bool
WaitSet::wait(int timeout_ms) {
  fds_.resize(slots_.size());
  for (size_t i = 0; i < slots_.size(); i++) fds_[i] = pollfd { slots_[i].fd, POLLIN, 0 };
  bool any = poll(fds_.data(), fds_.size(), timeout_ms) > 0;
  for (size_t i = 0; i < slots_.size(); i++) slots_[i].ready = any && fds_[i].revents;
  return any;
}
#else
bool
WaitSet::wait(int timeout_ms) {
  for (auto & slot : slots_) slot.ready = false;
  if (slots_.empty()) {
    if (timeout_ms > 0) Sleep(static_cast<DWORD>(timeout_ms));
    return false;
  }

  std::vector<HANDLE> handles;
  for (auto & slot : slots_) handles.push_back(slot.handle);
  auto result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE,
                                       timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms));
  if (result >= WAIT_OBJECT_0 + handles.size()) return false;

  // The call names one; all that are raised are ready, as with poll().
  for (auto & slot : slots_) slot.ready = WaitForSingleObject(slot.handle, 0) == WAIT_OBJECT_0;
  return true;
}
#endif

bool
WaitSet::ready(Group group) const {
  for (auto & slot : slots_) {
    if (slot.group == group && slot.ready) return true;
  }
  return false;
}
