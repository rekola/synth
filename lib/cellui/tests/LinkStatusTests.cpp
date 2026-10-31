#include "TestFramework.h"

#include <cellui/LinkStatus.h>
#ifndef _WIN32
#include <cellui/TestBackend.h>
#endif

using namespace cellui;

namespace {

using Ms = std::chrono::milliseconds;
const LinkClassifier::TimePoint t0 = LinkClassifier::TimePoint{} + std::chrono::seconds(100);
LinkClassifier::TimePoint at(int ms) { return t0 + Ms(ms); }

}  // namespace

TEST(link_class_names_parse_and_auto_clears_the_pin) {
  std::optional<LinkClass> pin = LinkClass::HZ_5;
  CHECK(parseLinkClass("auto", pin) && !pin);
  for (LinkClass cls : {LinkClass::FULL, LinkClass::HZ_30, LinkClass::HZ_15, LinkClass::HZ_5, LinkClass::HZ_1}) {
    CHECK(parseLinkClass(linkClassName(cls), pin) && pin == cls);
    CHECK(!linkClassLabel(cls).empty());
  }
  CHECK(!parseLinkClass("fast", pin) && pin == LinkClass::HZ_1);  // unchanged
}

TEST(link_class_for_bandwidth_is_the_best_class_it_is_enough_for) {
  LinkThresholds t;
  CHECK(linkClassForBandwidth(t, 20e6) == LinkClass::FULL);
  CHECK(linkClassForBandwidth(t, t.full) == LinkClass::FULL);
  CHECK(linkClassForBandwidth(t, t.full - 1) == LinkClass::HZ_30);
  CHECK(linkClassForBandwidth(t, 200e3) == LinkClass::HZ_15);
  CHECK(linkClassForBandwidth(t, 80e3) == LinkClass::HZ_5);
  CHECK(linkClassForBandwidth(t, 30e3) == LinkClass::HZ_1);
  // A margin asks for more than the threshold.
  CHECK(linkClassForBandwidth(t, t.full * 1.2, 1.5) == LinkClass::HZ_30);
  CHECK(linkClassForBandwidth(t, t.full * 1.5, 1.5) == LinkClass::FULL);
}

TEST(link_class_for_fps_follows_the_rate) {
  CHECK(linkClassForFps(60) == LinkClass::FULL);
  CHECK(linkClassForFps(58) == LinkClass::FULL);
  CHECK(linkClassForFps(30) == LinkClass::HZ_30);
  CHECK(linkClassForFps(15) == LinkClass::HZ_15);
  CHECK(linkClassForFps(6) == LinkClass::HZ_5);
  CHECK(linkClassForFps(2) == LinkClass::HZ_1);
}

TEST(classifier_goes_down_after_a_while_to_the_best_the_readings_said) {
  LinkClassifier c;
  CHECK(c.current() == LinkClass::FULL);
  c.update(at(0), LinkClass::HZ_15, LinkClass::HZ_15);
  c.update(at(1000), LinkClass::HZ_5, LinkClass::HZ_5);
  CHECK(c.current() == LinkClass::FULL);  // not yet
  c.update(at(2000), LinkClass::HZ_15, LinkClass::HZ_15);
  CHECK(c.current() == LinkClass::HZ_15);  // the best of 15, 5, 15: not the worst, and not one step
}

TEST(classifier_does_not_flap_on_a_noisy_estimate) {
  // 524 and 589 kB/s for a true 520: readings that cross a threshold now and then.
  LinkClassifier c;
  for (int i = 0; i < 120; i++) {
    LinkClass indicated = (i % 3 == 0) ? LinkClass::HZ_30 : LinkClass::FULL;  // a bad reading in three
    LinkClass margin = (i % 2 == 0) ? LinkClass::FULL : LinkClass::HZ_30;
    c.update(at(i * 1000), indicated, margin);
    CHECK(c.current() == LinkClass::FULL);
  }
  // And from below: readings just under the up margin never raise it.
  LinkClassifier d;
  d.update(at(0), LinkClass::HZ_30, LinkClass::HZ_30);
  d.update(at(2000), LinkClass::HZ_30, LinkClass::HZ_30);
  CHECK(d.current() == LinkClass::HZ_30);
  for (int i = 0; i < 100; i++) {
    d.update(at(3000 + i * 1000), LinkClass::FULL, LinkClass::HZ_30);  // enough, but not with the margin
    CHECK(d.current() == LinkClass::HZ_30);
  }
}

TEST(classifier_goes_up_slowly_and_one_class_at_a_time) {
  LinkClassifier c;
  c.update(at(0), LinkClass::HZ_5, LinkClass::HZ_5);
  c.update(at(2000), LinkClass::HZ_5, LinkClass::HZ_5);
  CHECK(c.current() == LinkClass::HZ_5);
  for (int s = 3; s < 18; s++) c.update(at(s * 1000), LinkClass::FULL, LinkClass::FULL);
  CHECK(c.current() == LinkClass::HZ_5);  // 14 seconds of better readings is not enough
  c.update(at(18000), LinkClass::FULL, LinkClass::FULL);
  CHECK(c.current() == LinkClass::HZ_15);  // one class
  for (int s = 19; s < 33; s++) c.update(at(s * 1000), LinkClass::FULL, LinkClass::FULL);
  CHECK(c.current() == LinkClass::HZ_15);
  c.update(at(33000), LinkClass::FULL, LinkClass::FULL);
  CHECK(c.current() == LinkClass::HZ_30);
}

TEST(classifier_pin_holds_whatever_is_read_and_unpinning_measures_again) {
  LinkClassifier c;
  c.pin(LinkClass::HZ_15);
  CHECK(c.current() == LinkClass::HZ_15 && c.pinned());
  for (int i = 0; i < 40; i++) c.update(at(i * 1000), LinkClass::FULL, LinkClass::FULL);
  CHECK(c.current() == LinkClass::HZ_15);
  c.pin(std::nullopt);
  CHECK(!c.pinned() && c.current() == LinkClass::HZ_15);  // measured from where it is
  for (int i = 0; i < 20; i++) c.update(at(50000 + i * 1000), LinkClass::FULL, LinkClass::FULL);
  CHECK(c.current() == LinkClass::HZ_30);
}

TEST(link_monitor_tells_the_handler_of_a_class_change_and_not_a_new_description) {
  LinkMonitor m;
  int calls = 0;
  LinkStatus last;
  m.setHandler([&](const LinkStatus & s) {
    calls++;
    last = s;
  });
  m.setDescription("480 kB/s");
  m.update(at(0), LinkClass::FULL, LinkClass::FULL);
  CHECK(calls == 1);  // the first reading makes the status available
  calls = 0;
  m.setDescription("470 kB/s");
  CHECK(calls == 0 && m.status().description == "470 kB/s");
  m.update(at(0), LinkClass::HZ_5, LinkClass::HZ_5);
  m.update(at(2000), LinkClass::HZ_5, LinkClass::HZ_5);
  CHECK(calls == 1 && last.cls == LinkClass::HZ_5 && !last.pinned);
  m.pin(LinkClass::HZ_30);
  CHECK(calls == 2 && last.cls == LinkClass::HZ_30 && last.pinned);
  m.pin(LinkClass::HZ_30);
  CHECK(calls == 2);  // nothing changed
  m.pin(std::nullopt);
  CHECK(calls == 3 && !last.pinned);
}

// The test backend is not built on Windows.
#ifndef _WIN32
TEST(test_backend_link_status_is_what_the_test_sets) {
  test::TestBackend backend;
  CHECK(backend.linkStatus().cls == LinkClass::FULL);
  int calls = 0;
  backend.setLinkHandler([&](const LinkStatus &) { calls++; });
  LinkStatus slow;
  slow.cls = LinkClass::HZ_15;
  slow.description = "300 kB/s";
  backend.setLinkStatus(slow);
  CHECK(calls == 1 && backend.linkStatus() == slow);
  backend.setLinkPin(LinkClass::HZ_5);
  CHECK(calls == 2 && backend.linkStatus().pinned && backend.linkStatus().cls == LinkClass::HZ_5);
}
#endif

TEST(a_monitor_is_available_after_an_update_or_when_told_so) {
  LinkMonitor m;
  int calls = 0;
  m.setHandler([&](const LinkStatus &) { calls++; });
  CHECK(!m.status().available);
  m.setAvailable(true);
  CHECK(m.status().available && calls == 1);
  m.setAvailable(true);
  CHECK(calls == 1);  // nothing changed
  LinkMonitor n;
  n.update(std::chrono::steady_clock::now(), LinkClass::FULL, LinkClass::FULL);
  CHECK(n.status().available);
}
