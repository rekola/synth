#include "TestFramework.h"

#include <cellui/Widget.h>

#include <memory>
#include <string>
#include <vector>

using namespace cellui;

namespace {

struct Ping {
  int n = 0;
};
struct Pong { };

using Log = std::vector<std::string>;

// Installs a Ping handler that logs `name` and answers `answer`.
Connection logPing(Widget & widget, Log & log, std::string name, Handled answer = Handled::no, Phase phase = Phase::bubble) {
  return widget.on<Ping>(
      [&log, name = std::move(name), answer](const Ping &, EventContext &) {
        log.push_back(name);
        return answer;
      },
      phase);
}

// root -> panel -> button
struct Chain {
  Widget root, panel, button;
  Chain() {
    panel.attach(root);
    button.attach(panel);
  }
};

// root -> a (a1, a2), b (b1)
struct Tree {
  Widget root, a, b, a1, a2, b1;
  Tree() {
    a.attach(root);
    b.attach(root);
    a1.attach(a);
    a2.attach(a);
    b1.attach(b);
  }
};

}  // namespace

TEST(targeted_post_runs_capture_from_the_root_down_then_bubble_back_up) {
  Chain t;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(t.root, log, "root bubble"));
  keep.push_back(logPing(t.panel, log, "panel bubble"));
  keep.push_back(logPing(t.button, log, "button bubble"));
  keep.push_back(logPing(t.button, log, "button capture", Handled::no, Phase::capture));
  keep.push_back(logPing(t.panel, log, "panel capture", Handled::no, Phase::capture));
  keep.push_back(logPing(t.root, log, "root capture", Handled::no, Phase::capture));
  CHECK(t.button.post(Ping{}) == Handled::no);
  CHECK(log == (Log{ "root capture", "panel capture", "button capture", "button bubble", "panel bubble", "root bubble" }));
}

TEST(an_ancestor_can_intercept_in_the_capture_phase) {
  Chain t;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(t.root, log, "root capture", Handled::no, Phase::capture));
  keep.push_back(logPing(t.panel, log, "panel capture", Handled::yes, Phase::capture));
  keep.push_back(logPing(t.button, log, "button capture", Handled::no, Phase::capture));
  keep.push_back(logPing(t.button, log, "button bubble"));
  keep.push_back(logPing(t.root, log, "root bubble"));
  CHECK(t.button.post(Ping{}) == Handled::yes);
  CHECK(log == (Log{ "root capture", "panel capture" }));
}

TEST(bubbling_stops_at_the_first_handler_that_takes_the_event) {
  Chain t;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(t.button, log, "button"));
  keep.push_back(logPing(t.panel, log, "panel", Handled::yes));
  keep.push_back(logPing(t.root, log, "root"));
  CHECK(t.button.post(Ping{}) == Handled::yes);
  CHECK(log == (Log{ "button", "panel" }));
}

TEST(a_targeted_post_reaches_only_the_path_and_only_its_type) {
  Tree t;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(t.a1, log, "a1"));
  keep.push_back(logPing(t.a2, log, "a2"));
  keep.push_back(logPing(t.b1, log, "b1"));
  keep.push_back(logPing(t.a, log, "a"));
  keep.push_back(t.a2.on<Pong>([&](const Pong &, EventContext &) {
    log.push_back("pong");
    return Handled::no;
  }));
  t.a2.post(Ping{});
  CHECK(log == (Log{ "a2", "a" }));
}

TEST(the_context_names_the_source_the_phase_and_the_loop_clock) {
  Chain t;
  t.root.setEventClock([] { return 42.5; });
  EventContext seen_capture, seen_bubble;
  auto c = t.root.on<Ping>(
      [&](const Ping &, EventContext & ctx) {
        seen_capture = ctx;
        return Handled::no;
      },
      Phase::capture);
  auto b = t.root.on<Ping>([&](const Ping &, EventContext & ctx) {
    seen_bubble = ctx;
    return Handled::no;
  });
  t.button.post(Ping{});
  CHECK(seen_capture.source == &t.button && seen_capture.phase == Phase::capture && seen_capture.time == 42.5);
  CHECK(seen_bubble.source == &t.button && seen_bubble.phase == Phase::bubble);
  CHECK(!seen_bubble.accepted);
}

TEST(the_handler_gets_the_event_unchanged) {
  Widget w;
  int seen = 0;
  auto c = w.on<Ping>([&](const Ping & p, EventContext &) {
    seen = p.n;
    return Handled::yes;
  });
  CHECK(w.post(Ping{ 7 }) == Handled::yes);
  CHECK(seen == 7);
}

TEST(a_root_post_visits_handlers_in_tree_pre_order_whatever_the_install_order) {
  Tree t;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(t.b1, log, "b1"));
  keep.push_back(logPing(t.a2, log, "a2"));
  keep.push_back(logPing(t.root, log, "root"));
  keep.push_back(logPing(t.b, log, "b"));
  keep.push_back(logPing(t.a1, log, "a1"));
  keep.push_back(logPing(t.a, log, "a"));
  CHECK(t.a2.postToRoot(Ping{}) == Handled::no);  // from anywhere: it goes to the root
  CHECK(log == (Log{ "root", "a", "a1", "a2", "b", "b1" }));
}

TEST(a_root_post_stops_at_the_first_handler_that_takes_it) {
  Tree t;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(t.root, log, "root"));
  keep.push_back(logPing(t.a, log, "a"));
  keep.push_back(logPing(t.a1, log, "a1", Handled::yes));
  keep.push_back(logPing(t.a2, log, "a2"));
  keep.push_back(logPing(t.b1, log, "b1"));
  CHECK(t.b1.postToRoot(Ping{}) == Handled::yes);
  CHECK(log == (Log{ "root", "a", "a1" }));
}

TEST(handlers_answering_no_let_every_widget_observe_the_event) {
  Tree t;
  Log log;
  std::vector<Connection> keep;
  for (Widget * w : { &t.root, &t.a, &t.b, &t.a1, &t.a2, &t.b1 }) keep.push_back(logPing(*w, log, "x"));
  CHECK(t.root.postToRoot(Ping{}) == Handled::no);
  CHECK(log.size() == 6);
}

TEST(one_widgets_handlers_run_in_install_order_until_one_takes_the_event) {
  Widget w;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(w, log, "first"));
  keep.push_back(logPing(w, log, "second", Handled::yes));
  keep.push_back(logPing(w, log, "third"));
  CHECK(w.postToRoot(Ping{}) == Handled::yes);
  CHECK(log == (Log{ "first", "second" }));
}

TEST(a_root_post_runs_capture_handlers_too_with_the_phase_set_to_bubble) {
  Chain t;
  Phase seen = Phase::capture;
  auto c = t.panel.on<Ping>(
      [&](const Ping &, EventContext & ctx) {
        seen = ctx.phase;
        return Handled::no;
      },
      Phase::capture);
  t.root.postToRoot(Ping{});
  CHECK(seen == Phase::bubble);
}

TEST(a_destroyed_connection_takes_its_handler_out_of_both_paths) {
  Chain t;
  Log log;
  {
    auto c = logPing(t.panel, log, "gone");
    t.button.post(Ping{});
    t.root.postToRoot(Ping{});
    CHECK(log.size() == 2);
  }
  t.button.post(Ping{});
  t.root.postToRoot(Ping{});
  CHECK(log.size() == 2);
}

TEST(a_widget_attached_after_installing_its_handler_is_found_in_its_place) {
  Tree t;
  Widget late;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(late, log, "late"));
  keep.push_back(logPing(t.root, log, "root"));
  keep.push_back(logPing(t.b1, log, "b1"));
  t.root.postToRoot(Ping{});
  CHECK(log == (Log{ "root", "b1" }));  // not part of the tree yet

  log.clear();
  late.attach(t.a);  // after a1 and a2
  t.root.postToRoot(Ping{});
  CHECK(log == (Log{ "root", "late", "b1" }));
}

TEST(a_reparented_widget_moves_to_its_new_place_and_leaves_the_old_tree) {
  Tree t;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(t.a, log, "a"));
  keep.push_back(logPing(t.b, log, "b"));
  keep.push_back(logPing(t.b1, log, "b1"));
  t.root.postToRoot(Ping{});
  CHECK(log == (Log{ "a", "b", "b1" }));

  log.clear();
  t.b1.attach(t.a);  // from under b to under a
  t.root.postToRoot(Ping{});
  CHECK(log == (Log{ "a", "b1", "b" }));
  CHECK(t.b1.parent() == &t.a);
  CHECK(t.b.children().empty());
}

TEST(a_widget_moved_to_another_tree_is_posted_to_there_only) {
  Tree one;
  Tree two;
  Log log;
  auto c = logPing(one.a1, log, "a1");
  one.a1.attach(two.b);
  one.root.postToRoot(Ping{});
  CHECK(log.empty());
  two.root.postToRoot(Ping{});
  CHECK(log == (Log{ "a1" }));
}

TEST(a_detached_widget_is_a_root_of_its_own) {
  Tree t;
  Log log;
  auto c = logPing(t.a1, log, "a1");
  auto r = logPing(t.root, log, "root");
  t.a1.detach();
  t.root.postToRoot(Ping{});
  CHECK(log == (Log{ "root" }));
  log.clear();
  t.a1.postToRoot(Ping{});
  CHECK(log == (Log{ "a1" }));
  CHECK(t.a1.parent() == nullptr);
}

TEST(a_widget_destroyed_between_installing_and_posting_is_not_called) {
  Tree t;
  Log log;
  std::vector<Connection> keep;
  keep.push_back(logPing(t.root, log, "root"));
  {
    auto doomed = std::make_unique<Widget>();
    doomed->attach(t.a);
    keep.push_back(logPing(*doomed, log, "doomed"));
    t.root.postToRoot(Ping{});
    CHECK(log == (Log{ "root", "doomed" }));
  }
  log.clear();
  t.root.postToRoot(Ping{});
  CHECK(log == (Log{ "root" }));
  CHECK(t.a.children().size() == 2);
}

TEST(a_connection_may_outlive_its_widget) {
  Connection c;
  Log log;
  {
    Widget w;
    c = logPing(w, log, "w");
    CHECK(c.connected());
  }
  CHECK(!c.connected());
  c.disconnect();
}

TEST(children_of_a_destroyed_parent_become_roots_that_still_receive) {
  auto parent = std::make_unique<Widget>();
  Widget child;
  child.attach(*parent);
  Log log;
  auto c = logPing(child, log, "child");
  parent.reset();
  CHECK(child.parent() == nullptr);
  child.postToRoot(Ping{});
  CHECK(log == (Log{ "child" }));
}

TEST(a_handler_may_destroy_a_widget_that_is_still_to_be_visited) {
  Tree t;
  std::unique_ptr<Widget> doomed = std::make_unique<Widget>();
  doomed->attach(t.a);  // after a1 and a2
  Log log;
  std::vector<Connection> keep;
  keep.push_back(t.a1.on<Ping>([&](const Ping &, EventContext &) {
    log.push_back("a1");
    doomed.reset();
    return Handled::no;
  }));
  keep.push_back(logPing(*doomed, log, "doomed"));
  keep.push_back(logPing(t.b, log, "b"));
  t.root.postToRoot(Ping{});
  CHECK(log == (Log{ "a1", "b" }));
}

TEST(a_handler_may_remove_itself_and_installs_made_meanwhile_wait_for_the_next_post) {
  Widget w;
  int self_calls = 0, added_calls = 0;
  Connection self;
  Connection added;
  self = w.on<Ping>([&](const Ping &, EventContext &) {
    self_calls++;
    self.disconnect();
    if (!added.connected()) {
      added = w.on<Ping>([&](const Ping &, EventContext &) {
        added_calls++;
        return Handled::no;
      });
    }
    return Handled::no;
  });
  w.postToRoot(Ping{});
  CHECK(self_calls == 1 && added_calls == 0);
  w.postToRoot(Ping{});
  CHECK(self_calls == 1 && added_calls == 1);
}

TEST(a_handler_may_post_another_event) {
  Chain t;
  Log log;
  auto pong = t.root.on<Pong>([&](const Pong &, EventContext &) {
    log.push_back("pong");
    return Handled::no;
  });
  auto ping = t.panel.on<Ping>([&](const Ping &, EventContext &) {
    log.push_back("ping");
    t.button.postToRoot(Pong{});
    return Handled::no;
  });
  t.button.post(Ping{});
  CHECK(log == (Log{ "ping", "pong" }));
}

TEST(a_tree_with_no_handlers_posts_to_nobody) {
  Tree t;
  CHECK(t.a1.post(Ping{}) == Handled::no);
  CHECK(t.a1.postToRoot(Ping{}) == Handled::no);
}
