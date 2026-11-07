#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

namespace cellui::term {

// The bytes of one frame by what they are: picture data, positioning of
// pictures, and the rest (text, cursor, wrappers, the fence).
struct FrameBytes {
  size_t upload = 0, placement = 0, other = 0;
  size_t total() const { return upload + placement + other; }
  FrameBytes & operator+=(const FrameBytes & o) {
    upload += o.upload;
    placement += o.placement;
    other += o.other;
    return *this;
  }
};

// Every constant the pacer chooses, in one place.
struct PacerConfig {
  using us = std::chrono::microseconds;
  // Frames are never closer than this, whatever the link could take.
  us min_frame_interval{16000};
  // In flight (sent, not acknowledged): at most this many bandwidth-delay
  // products of bytes, and always at least `min_in_flight_frames` times the
  // previous frame's size.
  double in_flight_bdp = 1.5;
  double min_in_flight_frames = 2;
  // Until the first frame is acknowledged, at most this many frames in flight.
  size_t startup_frames_in_flight = 2;
  // The bandwidth estimate is the largest of this many recent samples; a sample counts at most
  // `sample_cap` times the current estimate, and a lower window pulls the estimate down by `down_weight`
  // of the way.
  size_t sample_window = 8;
  // A saturation-grade frame's sample is the bytes of it and up to this many saturation-grade frames
  // before it, over the time since the reply to the frame before them: replies are read in batches, and
  // one frame's own time is too noisy where the link is the limit.
  size_t sample_frames = 4;
  double sample_cap = 2.0;
  double down_weight = 0.125;
  // Every frame is led by a fence of its own and followed by another, so the time from the reply to the
  // first to the reply to the second is the terminal taking in that frame alone, whether or not frames
  // go back to back. It makes a sample, S bytes over that time, only when it is at least `sample_floor`:
  // below it the clock, the batching of replies and the terminal's own timing say more than the link.
  // (Set by pacing checks 10 and 11: a busy terminal answering 5 to 40 ms late.)
  us sample_floor{10000};
  // The terminal's response delay D, the time from a lead fence to its reply, is kept as the minimum
  // over this long; it stands wherever a bare round trip would.
  us delay_window{5000000};
  // A sample may lower the estimate (or a frame count as queueing) only if it is saturation-grade: the
  // bytes in flight when its frame was sent were at least this share of B*D, so the pipe was full and
  // the frame's time is the link's. A frame sent as a probe, with S/B at least the floor, is probe-grade.
  // An app-limited sample can only raise the estimate. (Pacing checks 10 and 11, and the unit tests.)
  double saturation_fraction = 0.75;
  // Every `probe_every` frames one is sent `probe_gain` times faster than
  // the estimate, to find headroom. Probe frames are left out of the lag rule.
  int probe_every = 8;
  double probe_gain = 1.25;
  // Back off by `backoff_factor` when a saturation-grade frame is queueing - its reply later than
  // D + S/B + max(`queue_slack_d` * D, `queue_slack_floor`) after it was sent - `delay_frames` in a row,
  // or when `lag_frames` consecutive saturation-grade acknowledgements are `lag_ratio` times further
  // apart than the frames were sent.
  double backoff_factor = 0.8;
  double queue_slack_d = 2.0;
  us queue_slack_floor{20000};
  int delay_frames = 2;
  double lag_ratio = 1.5;
  int lag_frames = 4;
  // The link is taken to hold frames back only when the gap its estimate asks for is this many times
  // the floor between frames. Frames are never closer than the floor, so an estimate near the rate
  // they are sent at on the floor says only what was sent: small frames settle there, with the gap at
  // the floor and a little over, whatever the link could take.
  double link_bound_margin = 1.25;
  // No acknowledgement for this long halves the estimate; with none ever, or
  // this many times in a row, the terminal is taken not to answer.
  std::chrono::milliseconds stall{1500};
  int stalls_before_giving_up = 3;
  // What a terminal that does not answer gets.
  double fallback_fps = 4;
  // Bounds of the estimate in bytes per second; the upper one 0 for none
  // (a real serial line sets it from its speed).
  double min_bandwidth = 1000;
  double max_bandwidth = 0;
};

struct PacerStats {
  double bandwidth = 0;       // B, bytes per second; 0 until known
  double rtt_min_us = 0;      // the terminal's response delay D (windowed minimum), 0 until known
  double last_sample = 0;     // the last frame's own-time sample, bytes per second; 0 if none was valid
  // What the last acknowledged frame was good for: 0 nothing (no sample), 1 app-limited (may raise
  // the estimate only), 2 saturation-grade, 3 probe-grade (either may lower it).
  int last_grade = 0;
  double fps = 0;             // frames sent over the last second
  size_t bytes_in_flight = 0, frames_in_flight = 0;
  FrameBytes last_frame, totals;
  // What the last frame's size comes to at the floor between frames: the rate an estimate cannot
  // tell from the application's own when it is near it.
  double floor_rate = 0;
  size_t frames = 0, acks = 0, backoffs = 0, stalls = 0;
  size_t kitty_errors = 0;
  std::string last_kitty_error;
  bool fences = false;        // false: no answers to expect, fixed fallback_fps
};

// Decides when the next frame may start so that the link neither idles nor
// queues: frames are sent no faster than the link's bandwidth, as many in
// flight as its delay needs. It does no I/O and reads no clock: the caller
// reports what happened and when. A frame is followed on the wire by a
// fence (a cursor position query) whose reply is its acknowledgement, in
// order; the pacer learns the link from when the replies arrive.
class FramePacer {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  // `fences`: whether the terminal answers cursor position queries.
  explicit FramePacer(bool fences, PacerConfig config = {});

  // The state changed: a frame is wanted. Idempotent.
  void requestFrame() { dirty_ = true; }
  bool dirty() const { return dirty_; }

  // Whether a frame may start now (the caller also needs its output queue
  // to be empty).
  bool mayStart(TimePoint now) const;
  // Whether the link is what holds the next frame back: one is wanted and not allowed, and not
  // just by the floor between frames (the gap its size needs at the estimated bandwidth, or the
  // bytes already in flight). Only then is the bandwidth estimate a measurement of the link; when
  // the application is the limit it only says what the application sent.
  bool linkBound(TimePoint now) const;
  // When the time alone lets the next frame start, if one is wanted and
  // nothing else is holding it; also the moment a stall would be declared.
  // `queue_empty`: whether the output queue has drained; while it has not, a
  // frame cannot start and only the stall deadline counts.
  std::optional<TimePoint> deadline(bool queue_empty = true) const;
  // A frame is starting: the request is taken.
  // `now` is when the frame starts being made; the next one may start S/B
  // after it, whatever the time the bytes take to go (on a full link that
  // time is already the S/B).
  void onFrameStart(TimePoint now);

  // The fence in front of a frame has been written / the frame with the fence behind it has (every
  // byte accepted by the descriptor). `lead_bytes` are those of `size` that make the lead fence.
  void onLeadSent(TimePoint t_sent);
  void onFrameSent(FrameBytes size, TimePoint t_sent, size_t lead_bytes = 0);
  // The oldest outstanding fence was answered, `t_ack` being when the reply
  // was read.
  void onAck(TimePoint t_ack);
  // The time has passed: declares a stall.
  void tick(TimePoint now);
  // Replies that arrive for fences given up on: the caller drops them.
  size_t takeLateReplies() {
    size_t n = late_;
    late_ = 0;
    return n;
  }
  // The fence of this frame is not to be sent (an empty frame).
  bool fencesEnabled() const { return fences_; }

  double estimatedBytesPerSec() const { return bandwidth_; }
  PacerStats stats(TimePoint now) const;
  const PacerConfig & config() const { return config_; }

 private:
  struct Fence {
    bool lead = false;        // the fence in front of a frame, not the frame
    bool probing = false;     // a frame sent faster than the estimate
    size_t lead_bytes = 0;    // (frame) the bytes of its lead fence
    size_t in_flight_at_send = 0;  // (frame) bytes in flight when it was sent, its own included
    FrameBytes size;
    TimePoint started;  // when the frame began to be made: its first bytes went into the queue
    TimePoint sent;     // when its last byte was taken by the descriptor
  };
  size_t inFlightBytes() const;
  bool inFlightAllows() const;
  // Whether what is in flight leaves room for another frame.
  bool windowAllows() const;
  bool wouldProbe() const { return bandwidth_ > 0 && frames_since_probe_ >= config_.probe_every; }
  std::optional<TimePoint> gapEnd() const;
  void backOff();
  void giveUp();

  PacerConfig config_;
  bool fences_;
  bool dirty_ = false;
  bool starting_probe_ = false;  // the frame now starting is a probe frame

  double bandwidth_ = 0;
  // The terminal's response delay: lead fence to reply, each with when it was read.
  std::deque<std::pair<TimePoint, double>> delays_;
  double delayUs() const;  // the minimum over the window, 0 until known
  std::deque<Fence> in_flight_;
  struct Acked {
    TimePoint ack;
    double payload = 0;
    bool saturated = false;
  };
  std::deque<Acked> acked_;
  struct Sample {
    double rate = 0;
    bool may_lower = false;  // saturation- or probe-grade
  };
  std::deque<Sample> samples_;
  std::optional<TimePoint> lead_ack_;  // when the lead fence of the frame now to be answered was
  size_t late_ = 0;

  std::optional<TimePoint> last_start_;    // when the last frame that sent anything started
  TimePoint pending_start_{};
  std::optional<TimePoint> last_sent_;     // when its last byte went
  size_t last_sent_bytes_ = 0;
  bool last_sent_probing_ = false;
  std::optional<TimePoint> last_ack_;      // when the last fence of any kind was answered
  std::optional<TimePoint> last_end_ack_;  // when the last frame's own fence was
  std::optional<TimePoint> last_end_started_;  // when that frame was started
  bool last_end_probing_ = false;
  double last_sample_ = 0;
  int last_grade_ = 0;
  size_t frame_acks_ = 0;
  int frames_since_probe_ = 0;
  int delay_streak_ = 0;
  std::deque<double> lag_ratios_;
  int stall_streak_ = 0;
  bool answered_ = false;

  std::deque<TimePoint> recent_starts_;
  FrameBytes last_frame_, totals_;
  size_t frames_ = 0, acks_ = 0, backoffs_ = 0, stalls_ = 0;
};

}  // namespace cellui::term
