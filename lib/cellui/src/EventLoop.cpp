#include <cellui/EventLoop.h>

namespace cellui {

void EventLoop::run() {
  stops_.push_back(0);
  size_t level = stops_.size();
  while (!stops_[level - 1]) iterate(true);
  stops_.pop_back();
}

void EventLoop::quit() {
  if (!stops_.empty()) stops_.back() = 1;
}

}  // namespace cellui
