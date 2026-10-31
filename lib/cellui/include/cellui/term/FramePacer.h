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
  // The bandwidth estimate is the largest of this many recent back-to-back
  // samples; a sample counts at most `sample_cap` times the current estimate,
  // and a lower window pulls the estimate down by `down_weight` of the way.
  size_t sample_window = 8;
  // A sample is the bytes of up to this many frames sent back to back, over the time from the reply
  // before them to the last: replies come in batches, and one pair of them is too noisy.
  size_t sample_frames = 4;
  double sample_cap = 2.0;
  // Each frame is led by a fence of its own, so the time from the reply to that one to the reply to
  // the frame's own fence is the terminal taking in just that frame's bytes, with no idle time of
  // ours in it, whether or not frames were sent back to back. That time makes a sample of its own
  // when it is at least `sample_span`: below that the clock and the replies' batching say more than
  // the link does.
  bool start_fence = true;
  us sample_span{5000};
  double down_weight = 0.125;
  // Every `probe_every` frames one is sent `probe_gain` times faster than
  // the estimate, to find headroom. Probe frames are left out of the lag rule.
  int probe_every = 8;
  double probe_gain = 1.25;
  // Back off by `backoff_factor` when a frame's queueing delay exceeds
  // `delay_ratio` times RTT_min for `delay_frames` in a row (at least `delay_floor`), or when
  // `lag_frames` consecutive acknowledgements are `lag_ratio` times further
  // apart than the frames were sent.
  double backoff_factor = 0.8;
  double delay_ratio = 2.0;
  // The delay that counts is never less than this, so a link with an RTT of
  // microseconds does not back off at every scheduling jitter.
  us delay_floor{10000};
  int delay_frames = 2;
  double lag_ratio = 1.5;
  int lag_frames = 4;
  // RTT_min also takes the running minimum of the delay beyond transfer time
  // after this many acknowledgements.
  int rtt_refine_after = 10;
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
  double rtt_min_us = 0;      // 0 until known
  double fps = 0;             // frames sent over the last second
  size_t bytes_in_flight = 0, frames_in_flight = 0;
  FrameBytes last_frame, totals;
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

  // The bare query that precedes the first frame has been written / a frame
  // with its fence has (every byte accepted by the descriptor).
  void onProbeSent(TimePoint t_sent);
  // `lead_bytes`: those of `size` that are the fence in front of the frame, not part of the time
  // between the two replies.
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
  // The first frame goes with a bare query before it.
  bool needsProbe() const { return fences_ && !probe_sent_; }

  double estimatedBytesPerSec() const { return bandwidth_; }
  PacerStats stats(TimePoint now) const;
  const PacerConfig & config() const { return config_; }

 private:
  struct Fence {
    bool probe = false;       // the bare query, not a frame
    bool probing = false;     // a frame sent faster than the estimate
    bool lead = false;        // the fence in front of a frame
    size_t lead_bytes = 0;    // (frame fence) the bytes of its lead fence
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
  bool probe_sent_ = false;
  bool starting_probe_ = false;  // the frame now starting is a probe frame

  double bandwidth_ = 0;
  double rtt_min_us_ = 0;
  bool rtt_known_ = false;
  std::deque<Fence> in_flight_;
  std::deque<double> samples_;
  struct Acked {
    TimePoint ack;
    size_t bytes = 0;
    bool back_to_back = false;  // sent before the reply to the frame before it
  };
  std::deque<Acked> acked_;
  std::optional<TimePoint> lead_ack_;
  size_t late_ = 0;

  std::optional<TimePoint> last_start_;    // when the last frame that sent anything started
  TimePoint pending_start_{};
  std::optional<TimePoint> last_sent_;     // when its last byte went
  size_t last_sent_bytes_ = 0;
  bool last_sent_probing_ = false;
  std::optional<TimePoint> last_ack_;
  std::optional<TimePoint> last_ack_started_;  // when the frame last acknowledged was started
  bool last_ack_probing_ = false;
  std::optional<TimePoint> probe_ack_;     // when the bare query was answered
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
