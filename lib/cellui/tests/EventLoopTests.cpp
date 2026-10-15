#include "TestFramework.h"

#include <cellui/PollEventLoop.h>

#include <unistd.h>

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

}  // namespace

TEST(eventloop_runs_the_handler_of_a_ready_source) {
  PollEventLoop loop;
  Pipe a, b;
  int ran_a = 0, ran_b = 0;
  loop.addSource({ a.fds[0] }, [&] { ran_a++; a.drain(); });
  loop.addSource({ b.fds[0] }, [&] { ran_b++; b.drain(); });
  CHECK(!loop.iterate(false));
  b.raise();
  CHECK(loop.iterate(true));
  CHECK(ran_a == 0 && ran_b == 1);
  CHECK(!loop.iterate(false));
}

TEST(eventloop_source_of_several_fds_runs_once_per_turn) {
  PollEventLoop loop;
  Pipe a, b;
  int ran = 0;
  loop.addSource({ a.fds[0], b.fds[0] }, [&] { ran++; a.drain(); b.drain(); });
  a.raise();
  b.raise();
  loop.iterate(true);
  CHECK(ran == 1);
}

TEST(eventloop_hooks_bracket_a_turn_and_limit_the_wait) {
  PollEventLoop loop;
  std::string order;
  loop.setBefore([&] { order += "b"; return 5; });
  loop.setAfter([&](bool ready) { order += ready ? "R" : "t"; });
  CHECK(!loop.iterate(true));
  CHECK(order == "bt");
}

TEST(eventloop_run_ends_on_quit) {
  PollEventLoop loop;
  Pipe p;
  int turns = 0;
  loop.addSource({ p.fds[0] }, [&] { p.drain(); });
  loop.setBefore([] { return 1; });
  loop.setAfter([&](bool) { if (++turns == 3) loop.quit(); });
  loop.run();
  CHECK(turns == 3);
  CHECK(loop.depth() == 0);
  loop.quit();  // outside run(): nothing
  CHECK(loop.depth() == 0);
}

TEST(eventloop_runs_nest_and_quit_ends_the_innermost) {
  PollEventLoop loop;
  Pipe outer, inner;
  std::string log;
  int inner_depth = 0;
  loop.addSource({ outer.fds[0] }, [&] {
    outer.drain();
    log += "o";
    inner.raise();
    loop.run();  // a modal wait inside a handler
    log += "O";
    loop.quit();
  });
  loop.addSource({ inner.fds[0] }, [&] {
    inner.drain();
    log += "i";
    inner_depth = loop.depth();
    loop.quit();
  });
  outer.raise();
  loop.run();
  CHECK(log == "oiO");
  CHECK(inner_depth == 2);
  CHECK(loop.depth() == 0);
}
