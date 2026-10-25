#pragma once

#include <cellui/Signal.h>
#include <cellui/Wakeup.h>

#include <functional>
#include <memory>
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
//
// Every loop wakes for wake(), which any thread may call, and runs the frame
// hooks at the start of each turn, nested runs included.
class EventLoop {
 public:
  using Handler = std::function<void()>;

  virtual ~EventLoop() = default;
  EventLoop() = default;
  EventLoop(const EventLoop &) = delete;
  EventLoop & operator=(const EventLoop &) = delete;

  // `handler` runs, in a turn, when any of `fds` is readable. It must read
  // what made the descriptor ready: the sources are level-triggered.
  virtual void addSource(const std::vector<int> & fds, Handler handler) = 0;

  // The same on a platform whose sources are waitable handles (HANDLEs)
  // instead of descriptors; a loop that waits on descriptors ignores it.
  virtual void addHandles(const std::vector<void *> &, Handler) { }

  // Called at the start of every turn; returns the longest the turn may
  // wait in milliseconds (negative: as long as it takes).
  void setBefore(std::function<int()> before) { before_ = std::move(before); }
  // Called at the end of every turn, with whether any source was ready.
  void setAfter(std::function<void(bool)> after) { after_ = std::move(after); }

  // One turn: waits for a source (only for what is already ready when
  // `block` is false), runs the handlers of the ready ones in the order the
  // sources were added, and returns whether any was ready.
  virtual bool iterate(bool block) = 0;

  // Turns until quit() is called. `opener` is the object whose handler
  // makes the call (a dialog running its own loop): it is kept alive while
  // the run lasts, see deleteLater().
  void run(const void * opener = nullptr);
  // Ends the innermost run() after the turn it is in. Does nothing outside
  // run().
  void quit();
  // How many run()s are active.
  int depth() const { return static_cast<int>(stops_.size()); }

  // Ends the wait of the turn in progress, or starts the next turn at once.
  // Any thread; allocates nothing.
  void wake() { wakeup_.signal(); }

  // `fn` runs once per turn, before the turn waits, in nested runs too, until
  // the Connection is destroyed or disconnected. UI thread only.
  [[nodiscard]] Connection onFrame(std::function<void()> fn) { return frame_.connect(std::move(fn)); }

  // `fn` runs in the turn a wake() ended the wait of (or started), before the
  // other sources and the after hook, so what another thread posted is
  // handled in the turn it woke. UI thread only.
  [[nodiscard]] Connection onWake(std::function<void()> fn) { return woken_.connect(std::move(fn)); }

  // Destroys `object` once nothing running can still be using it: a handler
  // may delete the widget it is called from, or one a handler further out of
  // a nested run() is in. Outside any run() it is destroyed at once. Inside
  // one it lives until the end of that turn - and, if it is the opener of a
  // run() still active (the same pointer value), until that run has ended and
  // the turn that called it too. Whatever the loop still holds when it is
  // destroyed goes with it.
  template <class T>
  void deleteLater(std::unique_ptr<T> object) {
    const void * identity = object.get();
    defer(std::shared_ptr<void>(std::shared_ptr<T>(std::move(object))), identity);
  }

 protected:
  // A loop calls this once its sources can be added: the wakeup is one.
  void installWake();
  // The start of a turn: the frame hooks, then the before hook's answer.
  int beforeTurn() {
    frame_.emit();
    return before_ ? before_() : -1;
  }
  // The end of a turn.
  void afterTurn(bool ready) {
    if (after_) after_(ready);
    releaseDeferred(depth());
  }

 private:
  std::function<int()> before_;
  std::function<void(bool)> after_;
  // One flag per active run(), the innermost last.
  std::vector<char> stops_;
  // The opener each active run() was given, the innermost last.
  std::vector<const void *> openers_;

  struct Deferred {
    std::shared_ptr<void> object;
    const void * identity;
  };
  void woke();
  void defer(std::shared_ptr<void> object, const void * identity);
  // Destroys what was deferred at `level` that no active run() has as its
  // opener; the rest moves out a level.
  void releaseDeferred(int level);
  // By the depth the object was given up at.
  std::vector<std::vector<Deferred>> deferred_;

  Wakeup wakeup_;
  Signal<> frame_;
  Signal<> woken_;
};

}  // namespace cellui
