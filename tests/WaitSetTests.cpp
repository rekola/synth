#include "TestFramework.h"

#include "../src/util/WaitSet.h"

#include <chrono>
#include <thread>

using namespace std;
using namespace std::chrono;

TEST(wait_set_reports_which_group_is_ready_and_times_out_when_none) {
  Notifier a, b;
  WaitSet waits;
  auto first = waits.group();
  auto second = waits.group();
  waits.add(first, a);
  waits.add(second, b);

  CHECK(!waits.wait(0));
  CHECK(!waits.ready(first) && !waits.ready(second));

  b.signal();
  CHECK(waits.wait(1000));
  CHECK(!waits.ready(first));
  CHECK(waits.ready(second));

  // Still raised until cleared; both once both are.
  a.signal();
  CHECK(waits.wait(0));
  CHECK(waits.ready(first) && waits.ready(second));
  a.clear();
  b.clear();
  CHECK(!waits.wait(0));
}

TEST(wait_set_wakes_for_a_notifier_raised_from_another_thread) {
  Notifier n;
  WaitSet waits;
  auto g = waits.group();
  waits.add(g, n);
  thread raiser([&] {
    this_thread::sleep_for(milliseconds(20));
    n.signal();
  });
  CHECK(waits.wait(5000));
  CHECK(waits.ready(g));
  raiser.join();
}
