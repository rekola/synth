#pragma once

#include "../src/Controller.h"

#include <cellui/PollEventLoop.h>
#include <cellui/ThreadPosts.h>

#include <optional>

// Receives what the controller's threads post to the UI, as a UI would:
// take<E>() dispatches what waits and returns the last E among it.
class UICatcher {
 public:
  explicit UICatcher(Controller & controller) : controller_(controller) { controller_.setUIInbox(&inbox_); }
  ~UICatcher() { controller_.setUIInbox(nullptr); }
  UICatcher(const UICatcher &) = delete;
  UICatcher & operator=(const UICatcher &) = delete;

  template <class E>
  std::optional<E> take() {
    std::optional<E> got;
    auto connection = root_.on<E>([&](const E & event, cellui::EventContext &) {
      got.emplace(event);
      return cellui::Handled::no;
    });
    inbox_.drain();
    return got;
  }

  // How many posted events wait.
  size_t pending() const { return inbox_.pending(); }

 private:
  Controller & controller_;
  cellui::PollEventLoop loop_;
  cellui::Widget root_;
  cellui::ThreadInbox inbox_{loop_, root_};
};
