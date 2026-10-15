#pragma once

#include <cellui/EventLoop.h>

#include <memory>

namespace cellui::gtk {

// An EventLoop on GLib's main context: a turn is one
// g_main_context_iteration(), so the toolkit's own sources (windows, timers)
// are served in the same turns as the application's descriptors.
class GlibEventLoop : public EventLoop {
 public:
  GlibEventLoop();
  ~GlibEventLoop() override;
  GlibEventLoop(const GlibEventLoop &) = delete;
  GlibEventLoop & operator=(const GlibEventLoop &) = delete;

  void addSource(const std::vector<int> & fds, Handler handler) override;
  bool iterate(bool block) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cellui::gtk
