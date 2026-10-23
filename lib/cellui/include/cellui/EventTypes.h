#pragma once

#include <cellui/Signal.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace cellui {

class Widget;

// What a handler tells the dispatcher: Handled::yes stops the event there.
enum class Handled { no, yes };

// Where in a targeted dispatch a handler runs: capture goes from the root
// down to the target, bubble from the target back up.
enum class Phase { capture, bubble };

// Everything about one delivery that is not the event itself. Made on the
// stack by the dispatcher for each post and never stored with the event.
struct EventContext {
  Widget * source = nullptr;  // the widget a targeted event was posted from, null for a root post
  double time = 0;            // the loop clock when it was posted, in seconds
  Phase phase = Phase::bubble;
  bool accepted = false;      // some handler returned Handled::yes
};

// A key for an event type: the address of one variable per type, so no RTTI.
using TypeKey = const void *;

namespace detail {

template <class E>
struct TypeTag {
  static constexpr char tag = 0;
};

}  // namespace detail

template <class E>
constexpr TypeKey typeKey() {
  return &detail::TypeTag<E>::tag;
}

namespace detail {

struct HandlerSlot {
  uint64_t id;
  TypeKey type;
  Phase phase;
  std::function<Handled(const void *, EventContext &)> fn;
  bool alive;
};

// One widget's handlers. Shared with the Connections it hands out and with
// the dispatcher's snapshots, so it outlives the widget; `widget` is null
// once the widget is gone and the table does nothing then.
class HandlerTable : public SignalCore {
 public:
  Widget * widget = nullptr;

  uint64_t add(TypeKey type, Phase phase, std::function<Handled(const void *, EventContext &)> fn) {
    uint64_t id = ++last_id_;
    slots_.push_back(std::make_shared<HandlerSlot>(HandlerSlot{ id, type, phase, std::move(fn), true }));
    return id;
  }

  void disconnect(uint64_t id) override {
    for (auto & slot : slots_) {
      if (slot->id == id && slot->alive) {
        slot->alive = false;
        // Erasing mid-dispatch would shift the slots under its loop.
        if (depth_ == 0) compact();
        else dirty_ = true;
        return;
      }
    }
  }

  // The widget is gone: no handler runs again.
  void kill() {
    widget = nullptr;
    for (auto & slot : slots_) slot->alive = false;
    if (depth_ == 0) slots_.clear();
    else dirty_ = true;
  }

  bool has(TypeKey type) const {
    return std::any_of(slots_.begin(), slots_.end(), [&](const std::shared_ptr<HandlerSlot> & s) { return s->alive && s->type == type; });
  }

  std::vector<TypeKey> types() const {
    std::vector<TypeKey> out;
    for (auto & slot : slots_) {
      if (slot->alive && std::find(out.begin(), out.end(), slot->type) == out.end()) out.push_back(slot->type);
    }
    return out;
  }

  // Runs the live handlers for `type` (those of `phase`, if given) in
  // registration order until one returns Handled::yes. Handlers added during
  // the run wait for the next one.
  Handled run(TypeKey type, std::optional<Phase> phase, const void * event, EventContext & ctx) {
    depth_++;
    struct Leave {
      HandlerTable & table;
      ~Leave() {
        if (--table.depth_ == 0 && table.dirty_) table.compact();
      }
    } leave{ *this };
    size_t count = slots_.size();
    for (size_t i = 0; i < count; i++) {
      // Holding the slot keeps its function alive while it runs, even if it
      // disconnects itself or the widget goes.
      std::shared_ptr<HandlerSlot> slot = slots_[i];
      if (!slot->alive || slot->type != type) continue;
      if (phase && slot->phase != *phase) continue;
      if (slot->fn(event, ctx) == Handled::yes) return Handled::yes;
    }
    return Handled::no;
  }

 private:
  void compact() {
    slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [](const std::shared_ptr<HandlerSlot> & s) { return !s->alive; }), slots_.end());
    dirty_ = false;
  }

  std::vector<std::shared_ptr<HandlerSlot>> slots_;
  uint64_t last_id_ = 0;
  int depth_ = 0;
  bool dirty_ = false;
};

}  // namespace detail

}  // namespace cellui
