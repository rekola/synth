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

TEST(window_class_is_the_share_of_the_refresh_rate_achieved) {
  CHECK(linkClassForFrameRate(58, 60) == LinkClass::FULL);
  CHECK(linkClassForFrameRate(30, 30) == LinkClass::FULL);  // all a 30 Hz monitor shows
  CHECK(linkClassForFrameRate(60, 144) == LinkClass::FULL);  // FULL is 60 Hz and up
  CHECK(linkClassForFrameRate(30, 60) == LinkClass::HZ_30);
  CHECK(linkClassForFrameRate(15, 30) == LinkClass::HZ_30);  // half of what the monitor shows
  CHECK(linkClassForFrameRate(10, 60) == LinkClass::HZ_5);
  CHECK(linkClassForFrameRate(30, 0) == LinkClass::HZ_30);  // an unknown refresh rate is 60 Hz
  CHECK(linkClassForFrameRate(40, 60, 1.5) == LinkClass::HZ_30);
  // A frame rate cannot pass the refresh rate, so FULL needs no margin to be read as FULL.
  CHECK(linkClassForFrameRate(60, 60, 1.5) == LinkClass::FULL);
  CHECK(linkClassForFrameRate(30, 30, 1.5) == LinkClass::FULL);
}

namespace {

// A window's frame clock: it ticks every refresh interval, a tick that draws holds the next one back
// until its frame is done, and only every `every`th tick draws (an application capping its own rate).
// The window's reading is taken once a second, as the window does.
struct FakeClock {
  LinkMonitor link;
  FrameRateMeter meter;
  int64_t now_us = 100'000'000;
  int64_t next_reading_us = now_us + 1'000'000;
  int tick_count = 0;

  void run(double seconds, double refresh_hz, int64_t frame_us, int every = 1) {
    int64_t end = now_us + static_cast<int64_t>(seconds * 1e6);
    auto refresh_us = static_cast<int64_t>(1e6 / refresh_hz);
    while (now_us < end) {
      bool drew = tick_count++ % every == 0;
      meter.tick(now_us, drew);
      if (now_us >= next_reading_us) {
        updateWindowLink(link, t0 + std::chrono::microseconds(now_us), meter.rate(now_us), refresh_hz);
        next_reading_us += 1'000'000;
      }
      now_us += drew ? std::max(refresh_us, frame_us) : refresh_us;
    }
  }
  // Nothing drawn and no ticks: the clock is stopped.
  void idle(double seconds) {
    now_us += static_cast<int64_t>(seconds * 1e6);
    next_reading_us = now_us;
  }
};

}  // namespace

TEST(window_link_a_slow_frame_rate_lands_in_a_lower_class) {
  FakeClock clock;
  clock.run(5, 60, 0);
  CHECK(clock.link.status().cls == LinkClass::FULL);
  clock.run(5, 60, 77'000);  // 13 fps on a 60 Hz monitor
  CHECK(clock.link.status().cls == LinkClass::HZ_15);
}

TEST(window_link_a_30_hz_monitor_at_30_fps_is_full) {
  FakeClock clock;
  clock.run(20, 30, 0);
  CHECK(clock.link.status().available && clock.link.status().cls == LinkClass::FULL);
}

// An application at a low class draws only some ticks; the frames it draws are fast, so the class
// comes back up instead of staying where the cap holds it.
TEST(window_link_recovers_while_the_application_caps_its_own_rate) {
  FakeClock clock;
  clock.run(5, 60, 77'000);
  CHECK(clock.link.status().cls == LinkClass::HZ_15);
  clock.run(18, 60, 2'000, 4);  // a frame in four ticks: 15 a second, each quick
  CHECK(clock.link.status().cls == LinkClass::HZ_30);  // fifteen seconds up, one class
  clock.run(16, 60, 2'000, 2);  // 30 a second
  CHECK(clock.link.status().cls == LinkClass::FULL);
}

TEST(window_link_holds_while_nothing_is_drawn) {
  FakeClock clock;
  clock.run(5, 60, 77'000);
  CHECK(clock.link.status().cls == LinkClass::HZ_15);
  clock.run(10, 60, 0);  // a trend up begins
  clock.idle(30);        // a static UI: no ticks at all
  clock.run(6, 60, 0);
  CHECK(clock.link.status().cls == LinkClass::HZ_15);  // the trend starts again after the pause
  // Ticks that draw nothing measure nothing either: the class neither falls nor rises.
  clock.run(30, 60, 0, 1'000'000);
  CHECK(clock.link.status().cls == LinkClass::HZ_15);
}

TEST(window_link_a_stopped_clock_is_not_a_slow_frame) {
  FrameRateMeter meter;
  meter.tick(0, true);
  meter.tick(10'000'000, true);  // the window was hidden for ten seconds
  meter.tick(10'016'667, true);
  CHECK(!meter.rate(10'016'667));  // one frame is too few to tell
  meter.tick(10'033'333, true);
  CHECK(meter.rate(10'033'333) && *meter.rate(10'033'333) > 55);
}

TEST(window_link_a_pinned_class_is_kept_whatever_the_frame_rate) {
  FakeClock clock;
  clock.link.pin(LinkClass::HZ_15);
  clock.run(30, 60, 0);
  CHECK(clock.link.status().cls == LinkClass::HZ_15 && clock.link.status().pinned);
  clock.link.pin(std::nullopt);
  clock.run(16, 60, 0);
  CHECK(clock.link.status().cls == LinkClass::HZ_30);
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
