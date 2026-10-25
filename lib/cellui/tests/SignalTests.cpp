#include "TestFramework.h"

#include <cellui/Signal.h>

#include <memory>
#include <string>
#include <vector>

using namespace cellui;

TEST(signal_calls_handlers_in_connection_order_with_the_arguments) {
  Signal<int, std::string> s;
  std::vector<std::string> log;
  auto a = s.connect([&](int n, std::string t) { log.push_back("a" + std::to_string(n) + t); });
  auto b = s.connect([&](int n, std::string t) { log.push_back("b" + std::to_string(n) + t); });
  s.emit(7, "x");
  CHECK(log == (std::vector<std::string>{ "a7x", "b7x" }));
}

TEST(signal_with_no_handlers_emits_nothing) {
  Signal<> s;
  CHECK(s.empty());
  s.emit();
}

TEST(connection_disconnects_when_destroyed) {
  Signal<> s;
  int calls = 0;
  {
    auto c = s.connect([&] { calls++; });
    CHECK(c.connected());
    CHECK(s.size() == 1);
    s.emit();
  }
  s.emit();
  CHECK(calls == 1);
  CHECK(s.empty());
}

TEST(disconnect_is_explicit_and_idempotent) {
  Signal<> s;
  int calls = 0;
  auto c = s.connect([&] { calls++; });
  c.disconnect();
  c.disconnect();
  CHECK(!c.connected());
  s.emit();
  CHECK(calls == 0);
}

TEST(a_default_connection_is_inert) {
  Connection c;
  CHECK(!c.connected());
  c.disconnect();
}

TEST(connection_moves_and_the_source_lets_go) {
  Signal<> s;
  int calls = 0;
  Connection a = s.connect([&] { calls++; });
  Connection b = std::move(a);
  CHECK(!a.connected());
  CHECK(b.connected());
  a.disconnect();  // moved-from: must not remove b's handler
  s.emit();
  CHECK(calls == 1);

  // Assigning over a live connection disconnects what it held.
  int other = 0;
  Connection c = s.connect([&] { other++; });
  c = std::move(b);
  s.emit();
  CHECK(calls == 2 && other == 0);
}

TEST(a_handler_may_disconnect_itself) {
  Signal<> s;
  int calls = 0;
  Connection c;
  c = s.connect([&] {
    calls++;
    c.disconnect();
  });
  int after = 0;
  auto d = s.connect([&] { after++; });
  s.emit();
  s.emit();
  CHECK(calls == 1);
  CHECK(after == 2);
}

TEST(a_handler_may_disconnect_a_later_one_which_is_then_not_called) {
  Signal<> s;
  Connection later;
  int later_calls = 0;
  auto first = s.connect([&] { later.disconnect(); });
  later = s.connect([&] { later_calls++; });
  s.emit();
  CHECK(later_calls == 0);
  CHECK(s.size() == 1);
}

TEST(a_handler_may_disconnect_an_earlier_one) {
  Signal<> s;
  int first_calls = 0;
  Connection first = s.connect([&] { first_calls++; });
  auto second = s.connect([&] { first.disconnect(); });
  int third_calls = 0;
  auto third = s.connect([&] { third_calls++; });
  s.emit();
  s.emit();
  CHECK(first_calls == 1);
  CHECK(third_calls == 2);
}

TEST(handlers_connected_during_an_emission_wait_for_the_next) {
  Signal<> s;
  int added_calls = 0;
  Connection added;
  auto c = s.connect([&] {
    if (!added.connected()) added = s.connect([&] { added_calls++; });
  });
  s.emit();
  CHECK(added_calls == 0);
  s.emit();
  CHECK(added_calls == 1);
}

TEST(a_handler_may_emit_the_signal_again) {
  Signal<int> s;
  std::vector<int> seen;
  auto c = s.connect([&](int n) {
    seen.push_back(n);
    if (n > 0) s.emit(n - 1);
  });
  s.emit(2);
  CHECK(seen == (std::vector<int>{ 2, 1, 0 }));
}

TEST(a_disconnect_in_a_nested_emission_holds_for_the_outer_one) {
  Signal<int> s;
  Connection victim;
  int victim_calls = 0;
  auto a = s.connect([&](int n) {
    if (n == 1) s.emit(0);
  });
  auto killer = s.connect([&](int n) {
    if (n == 0) victim.disconnect();
  });
  victim = s.connect([&](int) { victim_calls++; });
  s.emit(1);
  // Gone in the nested emission, so the outer one skips it too; its slot is
  // only removed once the outer emission is done.
  CHECK(victim_calls == 0);
  CHECK(s.size() == 2);
  s.emit(1);
  CHECK(victim_calls == 0);
}

TEST(a_connection_outlives_its_signal) {
  Connection c;
  int calls = 0;
  {
    Signal<> s;
    c = s.connect([&] { calls++; });
    s.emit();
  }
  CHECK(!c.connected());
  c.disconnect();
  CHECK(calls == 1);
}

TEST(a_handler_may_destroy_the_owner_of_its_signal) {
  struct Owner {
    Signal<> changed;
  };
  auto owner = std::make_unique<Owner>();
  int later_calls = 0;
  auto a = owner->changed.connect([&] { owner.reset(); });
  auto b = owner->changed.connect([&] { later_calls++; });
  owner->changed.emit();
  CHECK(!owner);
  CHECK(later_calls == 0);  // the signal was gone before its turn
  CHECK(!a.connected());
}

TEST(a_handlers_captures_are_released_on_disconnect) {
  Signal<> s;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> watch = token;
  auto c = s.connect([token] { });
  token.reset();
  CHECK(!watch.expired());
  c.disconnect();
  CHECK(watch.expired());
}

TEST(a_handler_that_disconnects_itself_survives_the_call) {
  Signal<> s;
  auto token = std::make_shared<int>(41);
  Connection c;
  int seen = 0;
  c = s.connect([&, token] {
    c.disconnect();  // destroys the function's slot while it runs
    seen = *token + 1;
  });
  s.emit();
  CHECK(seen == 42);
}
