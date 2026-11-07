#include "TestFramework.h"

#include <cellui/term/FramePacer.h>

#include <cmath>

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

// A frame of `bytes` starting at `ms`, its lead fence and the frame both taken by the descriptor at once.
void send(FramePacer & p, size_t bytes, int ms) {
  p.requestFrame();
  p.onFrameStart(at(ms));
  p.onLeadSent(at(ms));
  p.onFrameSent(frame(bytes), at(ms));
}

// The replies to one frame's two fences, in the order they come.
void ack(FramePacer & p, int lead_ms, int end_ms) {
  p.onAck(at(lead_ms));
  p.onAck(at(end_ms));
}

// The start-up: a first frame at 0 whose lead fence is answered after `rtt` and its own after `rtt + transfer`.
void startUp(FramePacer & p, size_t bytes, int rtt, int transfer) {
  send(p, bytes, 0);
  ack(p, rtt, rtt + transfer);
}

}  // namespace

TEST(pacer_start_up_learns_the_response_delay_and_a_first_bandwidth) {
  FramePacer p(true);
  startUp(p, 10000, 100, 10);
  PacerStats s = p.stats(at(200));
  CHECK_NEAR(s.rtt_min_us, 100000.0, 1.0);
  CHECK_NEAR(s.bandwidth, 1000000.0, 1.0);  // 10000 bytes in the 10 ms between the two replies
  CHECK(s.acks == 2 && s.frames == 1 && s.bytes_in_flight == 0);
}

TEST(pacer_allows_one_more_frame_before_the_first_answer) {
  FramePacer p(true);
  p.requestFrame();
  CHECK(p.mayStart(at(0)));
  p.onFrameStart(at(0));
  p.onLeadSent(at(0));
  p.onFrameSent(frame(10000), at(0));
  p.requestFrame();
  CHECK(!p.mayStart(at(5)));  // inside the 16 ms floor
  CHECK(p.mayStart(at(20)));
  p.onFrameStart(at(20));
  p.onLeadSent(at(20));
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
    p.onLeadSent(at(ms));
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
  startUp(p, 10000, 1, 10);  // response delay 1 ms: the product is 1.5 kB
  send(p, 40000, 100);
  p.requestFrame();
  CHECK(p.mayStart(at(200)));  // 40 kB in flight against a cap of 2 x 40 kB
}

TEST(pacer_a_jittery_gap_raises_the_estimate_at_most_twofold) {
  FramePacer p(true);
  startUp(p, 10000, 100, 10);  // 1 MB/s
  send(p, 40000, 200);
  double before = p.estimatedBytesPerSec();
  ack(p, 300, 310);  // 40 kB in the 10 ms between the replies: 4 MB/s
  CHECK(p.estimatedBytesPerSec() <= 2.0 * before + 1);
  CHECK(p.estimatedBytesPerSec() > before);
}

TEST(pacer_a_frame_whose_own_time_is_under_the_floor_makes_no_sample) {
  FramePacer p(true);
  startUp(p, 10000, 100, 10);  // 1 MB/s
  double before = p.estimatedBytesPerSec();
  send(p, 10000, 200);
  ack(p, 300, 300);  // replies read together: a zero gap is no number
  CHECK(p.stats(at(400)).last_sample == 0 && p.stats(at(400)).last_grade == 0);
  send(p, 10000, 400);
  ack(p, 500, 505);  // 5 ms is under the 10 ms floor
  CHECK(p.stats(at(600)).last_sample == 0);
  CHECK(p.estimatedBytesPerSec() >= before);
}

TEST(pacer_a_saturated_slow_window_pulls_the_estimate_down_gradually) {
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 20, 10);  // 1 MB/s, response delay 20 ms
  double before = p.estimatedBytesPerSec();
  // The link now takes 20 ms a frame (500 kB/s), with frames queued up behind each other.
  for (int i = 0; i < 20; i++) send(p, 10000, 100 + 10 * i);
  int end = 150;
  double after_one = 0;
  for (int i = 0; i < 16; i++) {
    ack(p, end, end + 20);
    end += 20;
    if (i == 1) after_one = p.estimatedBytesPerSec();
  }
  double last = p.estimatedBytesPerSec();
  CHECK(after_one > 0.8 * before);  // one slow reply does not drop it far
  CHECK(p.stats(at(2000)).last_grade == 2);
  // ... and it settles near what the link does.
  CHECK(last < 0.7 * before && last > 300000);
}

TEST(pacer_a_link_of_30_kB_a_second_is_found_by_frames_of_7_5_kB) {
  // The picture quality a slow link gets makes frames of this size; they must be enough to find it.
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 7500, 20, 75);  // 100 kB/s to start with
  CHECK_NEAR(p.estimatedBytesPerSec(), 100000.0, 1.0);
  for (int i = 0; i < 20; i++) {
    int ms = 200 + 250 * i;
    send(p, 7500, ms);
    ack(p, ms + 20, ms + 20 + 250);  // 7.5 kB in 250 ms
  }
  CHECK(p.estimatedBytesPerSec() < 50000 && p.estimatedBytesPerSec() > 25000);
}

TEST(pacer_backs_off_when_a_saturated_frame_waits_long_twice_in_a_row) {
  FramePacer p(true);
  startUp(p, 10000, 10, 10);  // 1 MB/s, response delay 10 ms: 7.5 kB in flight fills the pipe
  double before = p.estimatedBytesPerSec();
  send(p, 10000, 200);
  send(p, 10000, 220);
  send(p, 10000, 240);
  ack(p, 210, 200 + 10 + 10 + 150);  // 150 ms longer than D, the transfer and the slack need
  CHECK(p.stats(at(500)).backoffs == 0);
  ack(p, 380, 220 + 10 + 10 + 150);
  CHECK(p.stats(at(500)).backoffs == 1);
  CHECK(p.estimatedBytesPerSec() < 0.85 * before);
}

TEST(pacer_a_frame_queueing_while_the_pipe_is_not_full_is_not_a_reason_to_back_off) {
  // A 40 ms response delay, 1 MB/s: 30 kB must be in flight to fill the pipe, and these frames are 3 kB.
  FramePacer p(true);
  startUp(p, 10000, 40, 10);
  for (int i = 0; i < 6; i++) {
    int ms = 200 + 100 * i;
    send(p, 3000, ms);
    ack(p, ms + 40, ms + 40 + 300);  // the end reply is far later than it should be
  }
  CHECK(p.stats(at(1000)).backoffs == 0);
}

TEST(pacer_a_delay_inside_the_slack_is_not_a_reason_to_back_off) {
  FramePacer p(true);
  startUp(p, 10000, 1, 10);  // response delay 1 ms
  for (int i = 0; i < 6; i++) {
    send(p, 10000, 100 + 10 * i);
    ack(p, 100 + 10 * i + 1, 100 + 10 * i + 1 + 10 + 15);  // 15 ms later than the transfer: under the 20 ms slack
  }
  CHECK(p.stats(at(500)).backoffs == 0);
}

TEST(pacer_the_response_delay_follows_a_terminal_that_slows_down_and_speeds_up) {
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 5, 10);  // idle: 5 ms
  CHECK_NEAR(p.stats(at(100)).rtt_min_us, 5000.0, 1.0);
  // Busy: every lead fence now answered 40 ms late. The 5 ms of a moment ago is still the minimum...
  int ms = 100;
  for (int i = 0; i < 10; i++, ms += 100) {
    send(p, 10000, ms);
    ack(p, ms + 40, ms + 50);
  }
  CHECK_NEAR(p.stats(at(ms)).rtt_min_us, 5000.0, 1.0);
  // ... until it is older than the window, when the terminal's delay is what it has been since.
  for (int i = 0; i < 60; i++, ms += 100) {
    send(p, 10000, ms);
    ack(p, ms + 40, ms + 50);
  }
  CHECK_NEAR(p.stats(at(ms)).rtt_min_us, 40000.0, 1.0);
  // Idle again: one quick answer is the minimum at once.
  send(p, 10000, ms);
  ack(p, ms + 5, ms + 15);
  CHECK_NEAR(p.stats(at(ms + 100)).rtt_min_us, 5000.0, 1.0);
}

TEST(pacer_a_sample_that_would_lower_the_estimate_is_ignored_when_the_pipe_was_not_full) {
  // 40 ms response delay at 1 MB/s: the pipe holds 40 kB. A terminal answering a 3 kB frame 35 ms after
  // its lead fence looks like 86 kB/s, which says nothing about the link when nothing else was in flight.
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 40, 10);
  double before = p.estimatedBytesPerSec();
  for (int i = 0; i < 40; i++) {
    int ms = 200 + 100 * i;
    send(p, 3000, ms);
    ack(p, ms + 40, ms + 75);
  }
  PacerStats s = p.stats(at(5000));
  CHECK(s.last_grade == 1 && s.last_sample > 0 && s.last_sample < 100000);  // a sample, but app-limited
  CHECK(p.estimatedBytesPerSec() >= before && s.backoffs == 0);
}

TEST(pacer_small_frames_with_late_replies_leave_the_estimate_alone_as_their_fence_gap_is_under_the_floor) {
  // A fast terminal drawing a nearly still screen: a kilobyte a frame, every reply 30 ms late, the gap
  // between the two replies of a frame a few milliseconds.
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 30, 10);  // 1 MB/s, the terminal answering in 30 ms
  double before = p.estimatedBytesPerSec();
  for (int i = 0; i < 60; i++) {
    int ms = 100 + 10 * i;
    send(p, 1000, ms);
    ack(p, ms + 30, ms + 38);
  }
  CHECK(p.stats(at(3000)).backoffs == 0);
  CHECK(p.estimatedBytesPerSec() >= before);
  CHECK(p.stats(at(3000)).last_sample == 0);
}

TEST(pacer_a_link_behind_a_small_buffer_is_found_though_each_last_byte_is_taken_late) {
  // A 25 kB frame takes 25 ms to drain at 1 MB/s; the reply comes 10 ms after its last byte. The
  // estimate starts at twice the link's.
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 10, 5);  // 2 MB/s: a bound that is twice too high
  CHECK(p.estimatedBytesPerSec() > 1.9e6);
  for (int i = 0; i < 40; i++) send(p, 25000, 100 + 25 * i);
  for (int i = 0; i < 40; i++) {
    int end = 135 + 25 * i;  // the link takes 25 ms a frame, the reply 10 ms after the last byte
    ack(p, i == 0 ? 110 : end - 25, end);
  }
  CHECK(p.estimatedBytesPerSec() < 1.3e6 && p.estimatedBytesPerSec() > 0.9e6);
}

TEST(pacer_replies_further_apart_than_the_frames_were_sent_back_a_saturated_pipe_off) {
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  cfg.lag_frames = 4;
  FramePacer p(true, cfg);
  startUp(p, 10000, 20, 10);  // 1 MB/s
  for (int i = 0; i < 12; i++) send(p, 10000, 100 + 10 * i);
  int end = 130;
  for (int i = 0; i < 12; i++) {
    ack(p, end, end + 18);  // a frame every 18 ms where they were sent 10 ms apart
    end += 18;
  }
  CHECK(p.stats(at(2000)).backoffs >= 1);
}

TEST(pacer_a_probe_frame_that_is_big_enough_can_lower_the_estimate_and_a_small_one_cannot) {
  PacerConfig cfg;
  cfg.min_frame_interval = Ms(1);
  FramePacer p(true, cfg);
  startUp(p, 10000, 100, 10);  // 1 MB/s, response delay 100 ms: 75 kB would fill the pipe
  int ms = 200;
  for (int i = 0; i < 8; i++, ms += 200) {
    send(p, 20000, ms);
    ack(p, ms + 100, ms + 120);  // 1 MB/s: the frames go as fast as the estimate says
  }
  double before = p.estimatedBytesPerSec();
  // The next frame is a probe, 1.25 times faster than the estimate. Small, its time says nothing.
  send(p, 1000, ms);
  ack(p, ms + 100, ms + 130);
  CHECK(p.stats(at(ms + 500)).last_grade == 1);
  CHECK(p.estimatedBytesPerSec() >= before);
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
  CHECK(p.takeLateReplies() == 4);  // two frames, two fences each
  CHECK(p.takeLateReplies() == 0);
}

TEST(pacer_a_terminal_that_never_answers_gets_a_fixed_rate) {
  FramePacer p(true);
  send(p, 10000, 0);
  p.tick(at(2000));
  PacerStats s = p.stats(at(2000));
  CHECK(!s.fences && s.stalls == 1);
  p.requestFrame();
  p.onFrameStart(at(2000));
  p.onFrameSent(frame(10000), at(2000));
  p.requestFrame();
  CHECK(!p.mayStart(at(2100)));
  CHECK(p.mayStart(at(2260)));  // 4 a second
}

TEST(pacer_without_fences_is_a_fixed_rate_from_the_start) {
  FramePacer p(false);
  CHECK(!p.fencesEnabled());
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

TEST(pacer_small_frames_near_the_floor_rate_are_not_the_link_holding_them_back) {
  FramePacer p(true);
  startUp(p, 630, 40, 10);  // an estimate of 63 kB/s
  send(p, 1150, 100);
  p.requestFrame();
  // 1150 bytes at 63 kB/s is a gap of 18 ms: a little over the floor, which is what small frames settle
  // at whatever the link could take.
  CHECK(!p.mayStart(at(105)));
  CHECK(!p.linkBound(at(105)));
  // Frames that would wait a long time at the estimate are the link holding them back.
  send(p, 6000, 200);
  p.requestFrame();
  CHECK(!p.mayStart(at(210)));
  CHECK(p.linkBound(at(210)));
}

TEST(pacer_a_first_frame_too_small_to_time_gives_the_floor_rate_as_a_bound_not_a_limit) {
  FramePacer p(true);
  startUp(p, 600, 40, 3);  // 3 ms: no number, only that the link did at least 600 bytes per floor interval
  CHECK_NEAR(p.estimatedBytesPerSec(), 600.0 / 0.016, 1.0);
}

TEST(pacer_stats_give_the_rate_a_frame_makes_at_the_floor_between_frames) {
  FramePacer p(true);
  startUp(p, 630, 40, 10);
  send(p, 1600, 100);
  CHECK(std::fabs(p.stats(at(101)).floor_rate - 100000.0) < 1.0);  // 1600 bytes every 16 ms
}
