#include "TestFramework.h"

#include <cellui/PollEventLoop.h>
#include <cellui/ThreadPosts.h>

#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace cellui;

namespace {

struct Pipe {
  int fds[2];
  Pipe() { CHECK(::pipe(fds) == 0); }
  ~Pipe() {
    ::close(fds[0]);
    ::close(fds[1]);
  }
  void raise() { CHECK(::write(fds[1], "x", 1) == 1); }
  void drain() {
    char c;
    CHECK(::read(fds[0], &c, 1) == 1);
  }
};

struct Number {
  int value;
};

// Turns the loop until `done`, at most `ms`.
template <class F>
bool turnUntil(EventLoop & loop, F done, int ms = 3000) {
  loop.setBefore([] { return 20; });
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (!done()) {
    if (std::chrono::steady_clock::now() > end) return false;
    loop.iterate(true);
  }
  return true;
}

// Records on which thread it was destroyed.
struct Tracker {
  explicit Tracker(std::vector<std::thread::id> * l) : log(l) { }
  std::vector<std::thread::id> * log;
  ~Tracker() { log->push_back(std::this_thread::get_id()); }
};

struct Carried {
  std::shared_ptr<Tracker> tracker;
  int value = 0;
};

}  // namespace

TEST(wakeup_is_ready_until_drained) {
  Wakeup w;
  pollfd p = { w.fd(), POLLIN, 0 };
  CHECK(::poll(&p, 1, 0) == 0);
  w.signal();
  w.signal();
  CHECK(::poll(&p, 1, 0) == 1);
  w.drain();
  CHECK(::poll(&p, 1, 0) == 0);
}

TEST(loop_wake_from_another_thread_ends_the_wait) {
  PollEventLoop loop;
  loop.setBefore([] { return 5000; });
  std::thread t([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop.wake();
  });
  auto start = std::chrono::steady_clock::now();
  CHECK(loop.iterate(true));
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
  t.join();
  CHECK(!loop.iterate(false));  // drained
}

TEST(loop_wake_before_the_wait_is_not_lost) {
  PollEventLoop loop;
  loop.setBefore([] { return 5000; });
  loop.wake();
  auto start = std::chrono::steady_clock::now();
  CHECK(loop.iterate(true));
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
}

TEST(on_frame_runs_every_turn_until_disconnected) {
  PollEventLoop loop;
  int frames = 0;
  Connection hook = loop.onFrame([&] { frames++; });
  loop.iterate(false);
  loop.iterate(false);
  CHECK(frames == 2);
  hook.disconnect();
  loop.iterate(false);
  CHECK(frames == 2);
}

TEST(on_frame_runs_in_nested_runs_too) {
  PollEventLoop loop;
  Pipe p;
  std::vector<int> depths;
  Connection hook = loop.onFrame([&] { depths.push_back(loop.depth()); });
  loop.addSource({ p.fds[0] }, [&] {
    p.drain();
    loop.run();  // nested
  });
  int turns = 0;
  loop.setAfter([&](bool) {
    if (loop.depth() == 2 && ++turns == 2) loop.quit();
    else if (loop.depth() == 1 && turns == 2) loop.quit();
  });
  loop.setBefore([] { return 1; });
  p.raise();
  loop.run();
  bool nested = false;
  for (int d : depths) nested |= d == 2;
  CHECK(nested);
}

TEST(inbox_delivers_on_the_ui_thread_in_order) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  std::vector<int> got;
  std::thread::id handled_on;
  Connection c = root.on<Number>([&](const Number & n, EventContext &) {
    got.push_back(n.value);
    handled_on = std::this_thread::get_id();
    return Handled::no;
  });
  std::atomic<int> refused{ 0 };
  std::thread t([&] {
    for (int i = 0; i < 200; i++) refused += inbox.post(Number{ i }) ? 0 : 1;
  });
  CHECK(turnUntil(loop, [&] { return got.size() == 200; }));
  t.join();
  CHECK(refused == 0);
  CHECK(handled_on == std::this_thread::get_id());
  for (int i = 0; i < 200; i++) CHECK(got[static_cast<size_t>(i)] == i);
}

TEST(inbox_destroys_the_payload_on_the_ui_thread) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  std::vector<std::thread::id> destroyed;
  int seen = 0;
  Connection c = root.on<Carried>([&](const Carried &, EventContext &) {
    seen++;
    return Handled::no;
  });
  std::atomic<bool> posted{ false };
  std::thread t([&] {
    Carried carried{ std::make_shared<Tracker>(&destroyed), 1 };
    posted = inbox.post(std::move(carried));
  });
  t.join();
  CHECK(posted);
  CHECK(turnUntil(loop, [&] { return seen == 1; }));
  CHECK(destroyed.size() == 1);
  CHECK(destroyed[0] == std::this_thread::get_id());
}

TEST(inbox_drops_an_event_nothing_handles) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  std::vector<std::thread::id> destroyed;
  CHECK(inbox.post(Carried{ std::make_shared<Tracker>(&destroyed), 1 }));
  inbox.drain();
  CHECK(destroyed.size() == 1);  // gone, with no handler to see it
}

TEST(inbox_high_water_refuses_when_full) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  inbox.setHighWater(2);
  CHECK(inbox.post(Number{ 1 }));
  CHECK(inbox.post(Number{ 2 }));
  CHECK(!inbox.post(Number{ 3 }));
  inbox.drain();
  CHECK(inbox.post(Number{ 4 }));
}

TEST(inbox_event_posted_by_a_handler_waits_for_the_next_drain) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  int seen = 0;
  Connection c = root.on<Number>([&](const Number & n, EventContext &) {
    seen++;
    if (n.value == 0) inbox.post(Number{ 1 });
    return Handled::no;
  });
  inbox.post(Number{ 0 });
  inbox.drain();
  CHECK(seen == 1);
  inbox.drain();
  CHECK(seen == 2);
}

TEST(inbox_is_drained_inside_a_nested_run_in_order) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  std::string log;
  struct Open { };
  struct Second { };
  Connection a = root.on<Open>([&](const Open &, EventContext &) {
    log += "open ";
    loop.run();
    log += "closed ";
    return Handled::no;
  });
  Connection b = root.on<Second>([&](const Second &, EventContext &) {
    log += "second ";
    loop.quit();
    return Handled::no;
  });
  inbox.post(Open{});
  inbox.post(Second{});
  loop.setBefore([] { return 1; });
  loop.iterate(false);
  CHECK(log == "open second closed ");
}

TEST(ring_delivers_in_order_and_wakes_the_loop) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  RealtimeRing & ring = inbox.addRing(8);
  std::vector<int> got;
  Connection c = root.on<Number>([&](const Number & n, EventContext &) {
    got.push_back(n.value);
    return Handled::no;
  });
  loop.setBefore([] { return 5000; });
  std::atomic<int> refused{ 0 };
  std::thread t([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    for (int i = 0; i < 3; i++) refused += ring.post(Number{ i }) ? 0 : 1;
  });
  auto start = std::chrono::steady_clock::now();
  while (got.size() < 3) loop.iterate(true);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
  t.join();
  CHECK(refused == 0);
  CHECK(got == (std::vector<int>{ 0, 1, 2 }));
}

TEST(ring_refuses_when_full_and_counts_it) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  RealtimeRing & ring = inbox.addRing(4);
  CHECK(ring.capacity() == 4);
  for (int i = 0; i < 4; i++) CHECK(ring.post(Number{ i }));
  CHECK(!ring.post(Number{ 4 }));
  CHECK(ring.dropped() == 1);
  std::vector<int> got;
  Connection c = root.on<Number>([&](const Number & n, EventContext &) {
    got.push_back(n.value);
    return Handled::no;
  });
  inbox.drain();
  CHECK(got == (std::vector<int>{ 0, 1, 2, 3 }));
  CHECK(ring.post(Number{ 5 }));  // room again
}

TEST(ring_rounds_its_size_up_to_a_power_of_two) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  CHECK(inbox.addRing(5).capacity() == 8);
  CHECK(inbox.addRing(1).capacity() == 2);
}

TEST(ring_carries_every_event_of_a_fast_producer_in_order) {
  PollEventLoop loop;
  Widget root;
  ThreadInbox inbox(loop, root);
  RealtimeRing & ring = inbox.addRing(64);
  const int kCount = 20000;
  std::vector<int> got;
  got.reserve(kCount);
  Connection c = root.on<Number>([&](const Number & n, EventContext &) {
    got.push_back(n.value);
    return Handled::no;
  });
  std::thread t([&] {
    for (int i = 0; i < kCount; i++) {
      while (!ring.post(Number{ i })) std::this_thread::yield();
    }
  });
  CHECK(turnUntil(loop, [&] { return got.size() == static_cast<size_t>(kCount); }, 20000));
  t.join();
  bool in_order = got.size() == static_cast<size_t>(kCount);
  for (size_t i = 0; in_order && i < got.size(); i++) in_order = got[i] == static_cast<int>(i);
  CHECK(in_order);
}

namespace {

// Logs its own destruction.
struct Logged {
  Logged(std::string * l, std::string n) : log(l), name(std::move(n)) { }
  std::string * log;
  std::string name;
  ~Logged() { *log += name + "-destroyed "; }
};

}  // namespace

TEST(delete_later_outside_a_run_destroys_at_once) {
  PollEventLoop loop;
  std::string log;
  loop.deleteLater(std::make_unique<Logged>(&log, "x"));
  CHECK(log == "x-destroyed ");
}

TEST(delete_later_waits_for_the_end_of_the_turn) {
  PollEventLoop loop;
  Pipe p;
  std::string log;
  loop.addSource({ p.fds[0] }, [&] {
    p.drain();
    loop.deleteLater(std::make_unique<Logged>(&log, "w"));
    log += "handler ";
  });
  loop.setAfter([&](bool) {
    log += "after ";
    loop.quit();
  });
  loop.setBefore([] { return 1; });
  p.raise();
  loop.run();
  CHECK(log == "handler after w-destroyed ");
}

TEST(delete_later_in_a_nested_run_is_destroyed_at_the_end_of_that_turn) {
  PollEventLoop loop;
  Pipe outer, inner;
  std::string log;
  loop.addSource({ outer.fds[0] }, [&] {
    outer.drain();
    log += "A ";
    inner.raise();
    loop.run();
    log += "A-resumed ";
  });
  loop.addSource({ inner.fds[0] }, [&] {
    inner.drain();
    loop.deleteLater(std::make_unique<Logged>(&log, "w"));
    log += "B ";
    loop.quit();
  });
  loop.setAfter([&](bool ready) {
    if (!ready) return;
    log += "after" + std::to_string(loop.depth()) + " ";
    if (loop.depth() == 1) loop.quit();
  });
  loop.setBefore([] { return 1; });
  outer.raise();
  loop.run();
  CHECK(log == "A B after2 w-destroyed A-resumed after1 ");
}

TEST(delete_later_keeps_the_opener_of_a_run_until_its_turn_is_over) {
  PollEventLoop loop;
  Pipe outer, inner;
  std::string log;
  auto dialog = std::make_unique<Logged>(&log, "dialog");
  Logged * raw = dialog.get();
  loop.addSource({ outer.fds[0] }, [&] {
    outer.drain();
    log += "A ";
    inner.raise();
    loop.run(raw);
    // The dialog was given up in the run, and is still here.
    log += "A-resumed(" + raw->name + ") ";
  });
  loop.addSource({ inner.fds[0] }, [&] {
    inner.drain();
    loop.deleteLater(std::move(dialog));
    log += "B ";
    loop.quit();
  });
  loop.setAfter([&](bool ready) {
    if (!ready) return;
    log += "after" + std::to_string(loop.depth()) + " ";
    if (loop.depth() == 1) loop.quit();
  });
  loop.setBefore([] { return 1; });
  outer.raise();
  loop.run();
  CHECK(log == "A B after2 A-resumed(dialog) after1 dialog-destroyed ");
}
