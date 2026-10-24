#pragma once

#include <cellui/EventLoop.h>
#include <cellui/Widget.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace cellui {

// A bounded single-producer single-consumer ring for a producer that must
// never allocate, lock or block (the audio thread). Made by
// ThreadInbox::addRing(), one per real-time producer.
//
// The payload is any trivially copyable event of at most kPayloadBytes. It is
// copied into a slot with a plain function pointer that posts it to the root
// on the UI thread; there is nothing to destroy.
class RealtimeRing {
 public:
  static constexpr size_t kPayloadBytes = 64;

  // The producer's call. False when the ring is full: the event is not sent
  // and the producer decides what to do (dropped() counts them). Wakes the
  // loop after pushing.
  template <class E>
  bool post(const E & event) noexcept {
    static_assert(std::is_trivially_copyable_v<E>, "a ring event is copied bytewise");
    static_assert(sizeof(E) <= kPayloadBytes, "a ring event is at most 64 bytes");
    static_assert(alignof(E) <= alignof(std::max_align_t), "a ring event must not be over-aligned");
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= slots_.size()) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    Slot & slot = slots_[head & mask_];
    std::memcpy(slot.bytes, &event, sizeof(E));
    slot.dispatch = &dispatchAs<E>;
    head_.store(head + 1, std::memory_order_release);
    loop_.wake();
    return true;
  }

  // How many posts found the ring full.
  size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  size_t capacity() const { return slots_.size(); }

 private:
  friend class ThreadInbox;

  struct Slot {
    alignas(std::max_align_t) unsigned char bytes[kPayloadBytes];
    void (*dispatch)(Widget &, const void *) = nullptr;
  };

  RealtimeRing(EventLoop & loop, size_t slots) : loop_(loop) {
    size_t n = 2;
    while (n < slots) n *= 2;
    slots_.resize(n);
    mask_ = n - 1;
  }

  template <class E>
  static void dispatchAs(Widget & root, const void * bytes) {
    root.postToRoot(*std::launder(static_cast<const E *>(bytes)));
  }

  // The consumer: how many are waiting, and takes the oldest.
  size_t available() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed); }
  bool take(Slot & out) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    out = slots_[tail & mask_];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  EventLoop & loop_;
  std::vector<Slot> slots_;
  size_t mask_ = 0;
  alignas(64) std::atomic<size_t> head_{ 0 };
  alignas(64) std::atomic<size_t> tail_{ 0 };
  std::atomic<size_t> dropped_{ 0 };
};

// Where other threads post events to a widget tree's root: they are
// dispatched on the UI thread like a Widget::postToRoot(), with a fresh
// EventContext, at the start of every turn of the loop (nested runs too) and
// in the turn a post woke it. An event nobody has a handler for any more is
// dropped.
//
// Two ways in, because the producers differ:
// - post(): any thread, any movable payload, may allocate and take a short
//   lock, never blocks the UI thread. The payload is destroyed on the UI
//   thread after dispatch.
// - addRing(): a ring for a producer that must not allocate or block.
// Each queue is FIFO; there is no order between the two. A call that returned
// true delivers exactly once. The inbox and the root must outlive the
// producers.
class ThreadInbox {
 public:
  ThreadInbox(EventLoop & loop, Widget & root) : loop_(loop), root_(root), frame_(loop.onFrame([this] { drain(); })), woken_(loop.onWake([this] { drain(); })) { }
  ThreadInbox(const ThreadInbox &) = delete;
  ThreadInbox & operator=(const ThreadInbox &) = delete;

  // Any thread. False when a high-water mark is set and that many events
  // wait: the event is not sent.
  template <class E>
  bool post(E event) {
    auto entry = std::make_unique<Entry<std::decay_t<E>>>(std::move(event));
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (high_water_ && queue_.size() >= high_water_) return false;
      queue_.push_back(std::move(entry));
      size_.store(queue_.size(), std::memory_order_relaxed);
    }
    loop_.wake();
    return true;
  }

  // How many posted events wait to be dispatched (the ones a drain has not
  // taken yet; rings not counted). Any thread.
  size_t pending() const { return size_.load(std::memory_order_relaxed); }

  // At most this many events wait in post()'s queue; 0, the default, is no
  // limit.
  void setHighWater(size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    high_water_ = limit;
  }

  // A ring of at least `slots` slots (a power of two) for one real-time
  // producer. UI thread, before the producer starts.
  RealtimeRing & addRing(size_t slots) {
    rings_.push_back(std::unique_ptr<RealtimeRing>(new RealtimeRing(loop_, slots)));
    return *rings_.back();
  }

  // Dispatches what is waiting (what was there when it started: an event
  // posted by a handler waits for the next turn). The loop's wake calls it;
  // calling it by hand is allowed. Reentrant, so a handler may run a
  // nested loop and the order stays FIFO. UI thread.
  void drain();

 private:
  struct Posted {
    virtual ~Posted() = default;
    virtual void dispatch(Widget & root) = 0;
  };
  template <class E>
  struct Entry : Posted {
    explicit Entry(E event) : event_(std::move(event)) { }
    void dispatch(Widget & root) override { root.postToRoot(event_); }
    E event_;
  };

  EventLoop & loop_;
  Widget & root_;
  std::mutex mutex_;
  std::deque<std::unique_ptr<Posted>> queue_;
  std::atomic<size_t> size_{ 0 };
  size_t high_water_ = 0;
  std::vector<std::unique_ptr<RealtimeRing>> rings_;
  // At the start of a turn, for what is already waiting (a nested run among
  // them), and in the turn a wake ended the wait of, so the after hook sees it.
  Connection frame_;
  Connection woken_;
};

}  // namespace cellui
