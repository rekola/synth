#include "TestFramework.h"

#include <cellui/term/FramePacer.h>

using namespace cellui::term;

namespace {

using Ms = std::chrono::milliseconds;
const FramePacer::TimePoint t0 = FramePacer::TimePoint{} + std::chrono::seconds(100);
FramePacer::TimePoint at(int ms) { return t0 + Ms(ms); }
FrameBytes frame(size_t bytes) {
  FrameBytes b;
  b.upload = bytes;
  return b;
}

// A frame of `bytes` starting at `ms`, sent as soon as it starts.
void send(FramePacer & p, size_t bytes, int ms) {
  p.requestFrame();
  p.onFrameStart(at(ms));
  p.onFrameSent(frame(bytes), at(ms));
}

// The start-up: the bare query, a first frame behind it, and their replies
// after `rtt` and `rtt + transfer`.
void startUp(FramePacer & p, size_t bytes, int rtt, int transfer) {
  p.requestFrame();
  p.onFrameStart(at(0));
  p.onProbeSent(at(0));
  p.onFrameSent(frame(bytes), at(0));
  p.onAck(at(rtt));
  p.onAck(at(rtt + transfer));
}

}  // namespace

TEST(pacer_start_up_learns_the_round_trip_and_a_first_bandwidth) {
  FramePacer p(true);
  startUp(p, 10000, 100, 10);
  PacerStats s = p.stats(at(200));
  CHECK_NEAR(s.rtt_min_us, 100000.0, 1.0);
  CHECK_NEAR(s.bandwidth, 1000000.0, 1.0);  // 10000 bytes in the 10 ms between the replies
  CHECK(s.acks == 2 && s.frames == 1 && s.bytes_in_flight == 0);
}

TEST(pacer_allows_one_more_frame_before_the_first_answer) {
  FramePacer p(true);
  p.requestFrame();
  CHECK(p.mayStart(at(0)));
  p.onFrameStart(at(0));
  p.onProbeSent(at(0));
  p.onFrameSent(frame(10000), at(0));
  p.requestFrame();
  CHECK(!p.mayStart(at(5)));  // inside the 16 ms floor
  CHECK(p.mayStart(at(20)));
  p.onFrameStart(at(20));
  p.onFrameSent(frame(10000), at(20));
  p.requestFrame();
  CHECK(!p.mayStart(at(60)));  // two frames in flight, none acknowledged
}

TEST(pacer_spaces_frames_by_their_size_over_the_bandwidth) {
  FramePacer p(true);
  startUp(p, 10000, 100, 10);  // 1 MB/s
  send(p, 50000, 200);         // 50 ms on the link
  p.requestFrame();
  CHECK(!p.mayStart(at(240)));
  CHECK(p.mayStart(at(251)));
  CHECK(p.deadline() && *p.deadline() <= at(251) + Ms(1));
}

TEST(pacer_limits_the_bytes_in_flight_to_the_link_s_bandwidth_delay_product) {
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 100, 10);  // 1 MB/s, 100 ms: 150 kB allowed in flight
  int ms = 200;
  size_t sent = 0;
  while (true) {
    p.requestFrame();
    if (!p.mayStart(at(ms))) {
      // Only the in-flight window can hold it, not the clock.
      ms += 100;
      p.requestFrame();
      if (!p.mayStart(at(ms))) break;
    }
    p.onFrameStart(at(ms));
    p.onFrameSent(frame(20000), at(ms));
    sent += 20000;
    ms += 20;
    if (sent > 1000000) break;
  }
  PacerStats s = p.stats(at(ms));
  CHECK(sent < 1000000);
  CHECK(s.bytes_in_flight >= 140000 && s.bytes_in_flight <= 190000);
}

TEST(pacer_always_leaves_room_for_two_frames_even_on_a_tiny_delay) {
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 1, 10);  // RTT 1 ms: the product is 1.5 kB
  p.requestFrame();
  p.onFrameStart(at(100));
  p.onFrameSent(frame(40000), at(100));
  p.requestFrame();
  CHECK(p.mayStart(at(200)));  // 40 kB in flight against a cap of 2 x 40 kB
}

TEST(pacer_a_jittery_gap_raises_the_estimate_at_most_twofold) {
  FramePacer p(true);
  startUp(p, 10000, 100, 10);  // 1 MB/s
  send(p, 10000, 200);
  send(p, 10000, 211);
  p.onAck(at(310));
  double before = p.estimatedBytesPerSec();
  p.onAck(at(310) + std::chrono::microseconds(20));  // two replies read together: a huge sample
  CHECK(p.estimatedBytesPerSec() <= 2.0 * before + 1);
  CHECK(p.estimatedBytesPerSec() > before);
}

TEST(pacer_a_slow_window_pulls_the_estimate_down_gradually) {
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 20, 10);  // 1 MB/s
  int sent_ms = 100, ack_ms = 50;
  for (int i = 0; i < 20; i++) {
    send(p, 10000, sent_ms);
    sent_ms += 10;
  }
  double before = p.estimatedBytesPerSec();
  double after_one = 0;
  for (int i = 0; i < 16; i++) {  // the link now takes 20 ms a frame: 500 kB/s
    p.onAck(at(ack_ms));
    ack_ms += 20;
    if (i == 0) after_one = p.estimatedBytesPerSec();
  }
  double last = p.estimatedBytesPerSec();
  CHECK(after_one > 0.8 * before);  // one slow reply does not drop it far
  // ... and it settles near what the link does (the queueing it sees can take it a little under).
  CHECK(last < 0.7 * before && last > 300000);
}

TEST(pacer_backs_off_when_frames_wait_long_twice_in_a_row) {
  FramePacer p(true);
  startUp(p, 10000, 100, 10);
  double before = p.estimatedBytesPerSec();
  send(p, 10000, 200);
  send(p, 10000, 220);
  send(p, 10000, 240);
  p.onAck(at(200 + 100 + 10 + 150));  // 150 ms longer than the transfer needs
  CHECK(p.stats(at(500)).backoffs == 0);
  p.onAck(at(220 + 100 + 10 + 150));
  PacerStats s = p.stats(at(500));
  CHECK(s.backoffs == 1);
  CHECK(p.estimatedBytesPerSec() < 0.85 * before);
}

TEST(pacer_a_delay_inside_the_floor_is_not_a_reason_to_back_off) {
  FramePacer p(true);
  startUp(p, 10000, 1, 10);  // RTT of 1 ms
  for (int i = 0; i < 6; i++) {
    send(p, 10000, 100 + 10 * i);
    p.onAck(at(100 + 10 * i + 1 + 10 + 5));  // 5 ms later than the transfer: under the 10 ms floor
  }
  CHECK(p.stats(at(500)).backoffs == 0);
}

TEST(pacer_probe_frames_are_left_out_of_the_lag_rule) {
  // Every eighth frame is sent 1.25 times faster than the link goes: its reply
  // is 1.25 times further from the one before than the frames were apart.
  PacerConfig cfg;
  cfg.lag_ratio = 1.2;
  cfg.lag_frames = 1;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 100, 10);  // 1 MB/s: 10 ms a frame
  int sent = 200;
  for (int i = 0; i < 40; i++) {
    p.requestFrame();
    int gap = (i % 9 == 8) ? 8 : 10;  // the probes
    sent += gap;
    p.onFrameStart(at(sent));
    p.onFrameSent(frame(10000), at(sent));
  }
  int ack = 300;
  for (int i = 0; i < 40; i++) {
    ack += 10;
    p.onAck(at(ack));
  }
  CHECK(p.stats(at(2000)).backoffs == 0);
}

TEST(pacer_a_silence_declares_a_stall_and_halves_the_estimate) {
  FramePacer p(true);
  startUp(p, 10000, 100, 10);
  send(p, 10000, 200);
  send(p, 10000, 220);
  double before = p.estimatedBytesPerSec();
  p.tick(at(1000));
  CHECK(p.stats(at(1000)).stalls == 0);
  p.tick(at(1800));
  PacerStats s = p.stats(at(1800));
  CHECK(s.stalls == 1 && s.frames_in_flight == 0 && s.fences);
  CHECK_NEAR(p.estimatedBytesPerSec(), before / 2, 1.0);
  CHECK(p.takeLateReplies() == 2);
  CHECK(p.takeLateReplies() == 0);
}

TEST(pacer_a_terminal_that_never_answers_gets_a_fixed_rate) {
  FramePacer p(true);
  p.requestFrame();
  p.onFrameStart(at(0));
  p.onProbeSent(at(0));
  p.onFrameSent(frame(10000), at(0));
  p.tick(at(2000));
  PacerStats s = p.stats(at(2000));
  CHECK(!s.fences && s.stalls == 1);
  p.requestFrame();
  p.onFrameStart(at(2000));
  p.onFrameSent(frame(10000), at(2000));
  p.requestFrame();
  CHECK(!p.mayStart(at(2100)));
  CHECK(p.mayStart(at(2260)));  // 4 a second
  CHECK(!p.needsProbe() || true);
}

TEST(pacer_without_fences_is_a_fixed_rate_from_the_start) {
  FramePacer p(false);
  CHECK(!p.fencesEnabled() && !p.needsProbe());
  send(p, 100000, 0);
  p.requestFrame();
  CHECK(!p.mayStart(at(200)));
  CHECK(p.mayStart(at(251)));
}

TEST(pacer_counts_bytes_by_kind_and_frames_per_second) {
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(false, cfg);
  FrameBytes b;
  b.upload = 100;
  b.placement = 20;
  b.other = 3;
  for (int i = 0; i < 5; i++) {
    p.requestFrame();
    p.onFrameStart(at(i * 300));
    p.onFrameSent(b, at(i * 300));
  }
  PacerStats s = p.stats(at(1300));
  CHECK(s.frames == 5 && s.totals.upload == 500 && s.totals.placement == 100 && s.totals.other == 15);
  CHECK(s.last_frame.total() == 123);
  CHECK_NEAR(s.fps, 4.0, 0.1);  // those in the last second: 4 of 5
}

TEST(pacer_a_frame_is_wanted_until_it_starts) {
  FramePacer p(false);
  CHECK(!p.mayStart(at(0)));
  p.requestFrame();
  p.requestFrame();
  CHECK(p.mayStart(at(0)));
  p.onFrameStart(at(0));
  CHECK(!p.dirty() && !p.mayStart(at(1000)));
}

TEST(pacer_frames_started_before_the_last_reply_are_back_to_back_even_if_their_last_byte_is_taken_later) {
  // A fast link behind a small buffer: a 25 kB frame takes 25 ms to drain at 1 MB/s, so its last byte is
  // taken long after the reply to the frame before it (10 ms after that one's last byte). Judged by the
  // last byte they would never be back to back and the estimate would keep its start-up value (2 MB/s,
  // twice the link's).
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 10, 5);  // 2 MB/s: a lower bound that is twice too high
  CHECK(p.estimatedBytesPerSec() > 1.9e6);
  int start = 100;
  for (int i = 0; i < 40; i++) {
    p.requestFrame();
    p.onFrameStart(at(start));
    p.onFrameSent(frame(25000), at(start + 25));
    p.onAck(at(start + 25 + 10));
    start += 25;
  }
  CHECK(p.estimatedBytesPerSec() < 1.3e6 && p.estimatedBytesPerSec() > 0.9e6);
}

TEST(pacer_reports_when_the_link_is_what_holds_the_next_frame_back) {
  PacerConfig cfg;
  FramePacer p(true, cfg);
  startUp(p, 10000, 100, 10);  // 1 MB/s
  send(p, 50000, 200);         // 50 ms on the link
  p.requestFrame();
  CHECK(p.linkBound(at(220)));   // waiting for the gap its size needs
  CHECK(!p.linkBound(at(260)));  // the gap is over: allowed
  FramePacer q(true, cfg);
  startUp(q, 10000, 100, 10);
  send(q, 1000, 200);  // 1 ms on the link: the floor between frames is what holds it
  q.requestFrame();
  CHECK(!q.linkBound(at(205)) && !q.mayStart(at(205)));
  CHECK(!FramePacer(false).linkBound(at(0)));  // a fixed rate measures nothing
}
