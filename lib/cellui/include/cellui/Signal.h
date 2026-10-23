#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace cellui {

namespace detail {

// What a Connection needs of the signal it came from, whatever its arguments.
class SignalCore {
 public:
  virtual ~SignalCore() = default;
  virtual void disconnect(uint64_t id) = 0;
};

struct ConnectionAccess;

}  // namespace detail

// A handler's place on a Signal. Move-only; disconnects when destroyed.
// Safe to outlive the signal (it then does nothing). UI thread only.
class Connection {
 public:
  Connection() = default;
  Connection(const Connection &) = delete;
  Connection & operator=(const Connection &) = delete;
  Connection(Connection && other) noexcept : core_(std::move(other.core_)), id_(other.id_) { other.id_ = 0; }
  Connection & operator=(Connection && other) noexcept {
    if (this != &other) {
      disconnect();
      core_ = std::move(other.core_);
      id_ = other.id_;
      other.id_ = 0;
    }
    return *this;
  }
  ~Connection() { disconnect(); }

  // Removes the handler; does nothing if already removed. A handler removed
  // during an emission is not called later in it.
  void disconnect() {
    if (auto core = core_.lock()) core->disconnect(id_);
    core_.reset();
    id_ = 0;
  }

  // Whether the handler is still installed on a signal that still exists.
  bool connected() const { return id_ != 0 && !core_.expired(); }

 private:
  template <class...>
  friend class Signal;
  friend struct detail::ConnectionAccess;
  Connection(std::weak_ptr<detail::SignalCore> core, uint64_t id) : core_(std::move(core)), id_(id) { }

  std::weak_ptr<detail::SignalCore> core_;
  uint64_t id_ = 0;
};

namespace detail {

// For the classes that hand out Connections to handlers they keep.
struct ConnectionAccess {
  static Connection make(std::weak_ptr<SignalCore> core, uint64_t id) { return Connection(std::move(core), id); }
};

}  // namespace detail

// A point-to-point observer: the object that changes owns one, and whoever
// holds that object connects a handler. Emission is synchronous, in
// connection order, on the calling thread. A handler may disconnect itself or
// any other, connect new ones (called from the next emission on), emit the
// signal again or destroy its owner. Not thread-safe, and not movable: a
// connection names the signal it was made on.
template <class... Args>
class Signal {
 public:
  Signal() : state_(std::make_shared<State>()) { }
  Signal(const Signal &) = delete;
  Signal & operator=(const Signal &) = delete;
  Signal(Signal &&) = delete;
  Signal & operator=(Signal &&) = delete;
  ~Signal() {
    if (state_) state_->clear();
  }

  [[nodiscard]] Connection connect(std::function<void(Args...)> fn) {
    uint64_t id = ++state_->last_id;
    state_->slots.push_back(std::make_shared<Slot>(Slot{ id, std::move(fn), true }));
    return Connection(state_, id);
  }

  void emit(Args... args) const {
    // A handler may destroy the object that owns this signal.
    std::shared_ptr<State> keep = state_;
    State & state = *keep;
    // Handlers connected from here on wait for the next emission.
    size_t count = state.slots.size();
    state.depth++;
    struct DepthGuard {
      State & state;
      ~DepthGuard() {
        if (--state.depth == 0 && state.dirty) state.compact();
      }
    } guard{ state };
    for (size_t i = 0; i < count && i < state.slots.size(); i++) {
      // Holding the slot keeps its function alive while it runs, even if it
      // disconnects itself.
      std::shared_ptr<Slot> slot = state.slots[i];
      if (slot->alive) slot->fn(args...);
    }
  }

  size_t size() const { return state_->live(); }
  bool empty() const { return size() == 0; }

 private:
  struct Slot {
    uint64_t id;
    std::function<void(Args...)> fn;
    bool alive;
  };

  struct State : detail::SignalCore {
    std::vector<std::shared_ptr<Slot>> slots;
    uint64_t last_id = 0;
    int depth = 0;
    bool dirty = false;

    void disconnect(uint64_t id) override {
      for (auto & slot : slots) {
        if (slot->id == id && slot->alive) {
          slot->alive = false;
          // Erasing mid-emission would shift the slots under its loop.
          if (depth == 0) compact();
          else dirty = true;
          return;
        }
      }
    }
    void compact() {
      slots.erase(std::remove_if(slots.begin(), slots.end(), [](const std::shared_ptr<Slot> & s) { return !s->alive; }), slots.end());
      dirty = false;
    }
    void clear() {
      for (auto & slot : slots) slot->alive = false;
      if (depth == 0) slots.clear();
      else dirty = true;
    }
    size_t live() const {
      return static_cast<size_t>(std::count_if(slots.begin(), slots.end(), [](const std::shared_ptr<Slot> & s) { return s->alive; }));
    }
  };

  std::shared_ptr<State> state_;
};

}  // namespace cellui
