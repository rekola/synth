#pragma once

#include <functional>
#include <vector>

namespace cellui {

// What an application waits with: sources (file descriptors that become
// readable), a limit on how long a turn may wait, and a place to act when a
// turn is done. A backend owns the loop that suits it, one that waits on
// descriptors itself or the toolkit's own, so the application's loop is the
// same code with either.
//
// Re-entrant: a handler may call run() again (a modal dialog waiting for its
// answer), and quit() ends the innermost run().
class EventLoop {
 public:
  using Handler = std::function<void()>;

  virtual ~EventLoop() = default;

  // `handler` runs, in a turn, when any of `fds` is readable. It must read
  // what made the descriptor ready: the sources are level-triggered.
  virtual void addSource(const std::vector<int> & fds, Handler handler) = 0;

  // Called at the start of every turn; returns the longest the turn may
  // wait in milliseconds (negative: as long as it takes).
  void setBefore(std::function<int()> before) { before_ = std::move(before); }
  // Called at the end of every turn, with whether any source was ready.
  void setAfter(std::function<void(bool)> after) { after_ = std::move(after); }

  // One turn: waits for a source (only for what is already ready when
  // `block` is false), runs the handlers of the ready ones in the order the
  // sources were added, and returns whether any was ready.
  virtual bool iterate(bool block) = 0;

  // Turns until quit() is called.
  void run();
  // Ends the innermost run() after the turn it is in. Does nothing outside
  // run().
  void quit();
  // How many run()s are active.
  int depth() const { return static_cast<int>(stops_.size()); }

 protected:
  int beforeTurn() const { return before_ ? before_() : -1; }
  void afterTurn(bool ready) const {
    if (after_) after_(ready);
  }

 private:
  std::function<int()> before_;
  std::function<void(bool)> after_;
  // One flag per active run(), the innermost last.
  std::vector<char> stops_;
};

}  // namespace cellui
