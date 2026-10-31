#include <cellui/term/FramePacer.h>

#include <algorithm>

namespace cellui::term {

namespace {

using std::chrono::duration;
using std::chrono::duration_cast;

double seconds(FramePacer::TimePoint later, FramePacer::TimePoint earlier) { return duration<double>(later - earlier).count(); }

FramePacer::TimePoint plus(FramePacer::TimePoint t, double secs) {
  return t + duration_cast<FramePacer::Clock::duration>(duration<double>(secs));
}

}  // namespace

FramePacer::FramePacer(bool fences, PacerConfig config) : config_(config), fences_(fences) { }

size_t FramePacer::inFlightBytes() const {
  size_t n = 0;
  for (const Fence & f : in_flight_) n += f.size.total();
  return n;
}

bool FramePacer::inFlightAllows() const {
  double cap = std::max(config_.in_flight_bdp * bandwidth_ * rtt_min_us_ * 1e-6,
                        config_.min_in_flight_frames * static_cast<double>(last_sent_bytes_));
  return static_cast<double>(inFlightBytes()) <= cap;
}

std::optional<FramePacer::TimePoint> FramePacer::gapEnd() const {
  if (!last_start_) return std::nullopt;
  double gap = duration<double>(config_.min_frame_interval).count();
  if (!fences_) {
    gap = std::max(gap, 1.0 / config_.fallback_fps);
  } else if (bandwidth_ > 0) {
    double rate = bandwidth_ * (wouldProbe() ? config_.probe_gain : 1.0);
    gap = std::max(gap, static_cast<double>(last_sent_bytes_) / rate);
  }
  return plus(*last_start_, gap);
}

bool FramePacer::windowAllows() const {
  if (!fences_) return true;
  if (bandwidth_ <= 0) {
    // Before the first answer is in, one more frame than the first.
    size_t frames = 0;
    for (const Fence & f : in_flight_) frames += f.probe ? 0 : 1;
    return frames < config_.startup_frames_in_flight;
  }
  return inFlightAllows();
}

bool FramePacer::mayStart(TimePoint now) const {
  if (!dirty_) return false;
  auto end = gapEnd();
  if (end && now < *end) return false;
  return windowAllows();
}

bool FramePacer::linkBound(TimePoint now) const {
  if (!fences_ || !dirty_ || bandwidth_ <= 0 || mayStart(now)) return false;
  if (!windowAllows()) return true;
  if (!last_start_) return false;
  double floor = duration<double>(config_.min_frame_interval).count();
  double gap = static_cast<double>(last_sent_bytes_) / (bandwidth_ * (wouldProbe() ? config_.probe_gain : 1.0));
  return gap > floor && now < plus(*last_start_, gap);
}

std::optional<FramePacer::TimePoint> FramePacer::deadline(bool queue_empty) const {
  std::optional<TimePoint> best;
  auto consider = [&](TimePoint t) {
    if (!best || t < *best) best = t;
  };
  if (dirty_ && queue_empty) {
    if (windowAllows()) {
      if (auto end = gapEnd()) consider(*end);
      else consider(TimePoint{});
    }
  }
  if (!in_flight_.empty()) {
    TimePoint ref = in_flight_.front().sent;
    if (last_ack_ && *last_ack_ > ref) ref = *last_ack_;
    consider(ref + config_.stall);
  }
  return best;
}

void FramePacer::onFrameStart(TimePoint now) {
  dirty_ = false;
  pending_start_ = now;
  starting_probe_ = wouldProbe();
}

void FramePacer::onProbeSent(TimePoint t_sent) {
  probe_sent_ = true;
  if (!fences_) return;
  Fence f;
  f.probe = true;
  f.sent = t_sent;
  in_flight_.push_back(f);
}

void FramePacer::onFrameSent(FrameBytes size, TimePoint t_sent) {
  frames_++;
  last_frame_ = size;
  totals_ += size;
  recent_starts_.push_back(t_sent);
  while (!recent_starts_.empty() && t_sent - recent_starts_.front() > std::chrono::seconds(1)) recent_starts_.pop_front();
  last_sent_ = t_sent;
  last_start_ = pending_start_;
  last_sent_bytes_ = size.total();
  last_sent_probing_ = starting_probe_;
  frames_since_probe_ = starting_probe_ ? 0 : frames_since_probe_ + 1;
  if (fences_) {
    Fence f;
    f.probing = starting_probe_;
    f.size = size;
    f.sent = t_sent;
    in_flight_.push_back(f);
  }
  starting_probe_ = false;
}

void FramePacer::backOff() {
  bandwidth_ = std::max(config_.min_bandwidth, bandwidth_ * config_.backoff_factor);
  samples_.clear();
  lag_ratios_.clear();
  delay_streak_ = 0;
  backoffs_++;
}

void FramePacer::giveUp() {
  fences_ = false;
  in_flight_.clear();
}

void FramePacer::onAck(TimePoint t_ack) {
  if (in_flight_.empty()) return;
  Fence f = in_flight_.front();
  in_flight_.pop_front();
  acks_++;
  answered_ = true;
  stall_streak_ = 0;
  auto micros = [](TimePoint a, TimePoint b) { return duration<double, std::micro>(a - b).count(); };

  if (f.probe) {
    rtt_min_us_ = std::max(0.0, micros(t_ack, f.sent));
    rtt_known_ = true;
    probe_ack_ = t_ack;
    last_ack_ = t_ack;
    return;
  }
  frame_acks_++;
  double size = static_cast<double>(f.size.total());
  if (bandwidth_ <= 0) {
    // The first frame, sent right behind the query: a lower bound, the probes raise it.
    double gap = probe_ack_ && t_ack > *probe_ack_ ? seconds(t_ack, *probe_ack_) : seconds(t_ack, f.sent);
    bandwidth_ = size / std::max(gap, 1e-4);
    if (!rtt_known_) {
      rtt_min_us_ = std::max(0.0, micros(t_ack, f.sent));
      rtt_known_ = true;
    }
  } else {
    double delay = micros(t_ack, f.sent) - size / bandwidth_ * 1e6;
    if (frame_acks_ > static_cast<size_t>(config_.rtt_refine_after)) rtt_min_us_ = std::min(rtt_min_us_, std::max(0.0, delay));
    if (last_ack_ && last_ack_sent_ && f.sent <= *last_ack_) {
      // Sent back to back with the one before: the gap between the replies
      // is the time the link took for this frame.
      double gap = seconds(t_ack, *last_ack_);
      if (gap > 1e-5) {
        double sample = std::min(size / gap, config_.sample_cap * bandwidth_);
        samples_.push_back(sample);
        while (samples_.size() > config_.sample_window) samples_.pop_front();
        double target = *std::max_element(samples_.begin(), samples_.end());
        if (target >= bandwidth_) bandwidth_ = target;
        else bandwidth_ += config_.down_weight * (target - bandwidth_);
        double send_gap = seconds(f.sent, *last_ack_sent_);
        if (!f.probing && !last_ack_probing_ && send_gap > 1e-5) {
          lag_ratios_.push_back(gap / send_gap);
          while (lag_ratios_.size() > static_cast<size_t>(config_.lag_frames)) lag_ratios_.pop_front();
          if (lag_ratios_.size() == static_cast<size_t>(config_.lag_frames) &&
              std::all_of(lag_ratios_.begin(), lag_ratios_.end(), [&](double r) { return r > config_.lag_ratio; })) {
            backOff();
          }
        }
      }
    }
    if (rtt_known_ && delay > std::max(config_.delay_ratio * rtt_min_us_, static_cast<double>(config_.delay_floor.count()))) delay_streak_++;
    else delay_streak_ = 0;
    if (delay_streak_ >= config_.delay_frames) backOff();
  }
  bandwidth_ = std::max(bandwidth_, config_.min_bandwidth);
  if (config_.max_bandwidth > 0) bandwidth_ = std::min(bandwidth_, config_.max_bandwidth);
  last_ack_ = t_ack;
  last_ack_sent_ = f.sent;
  last_ack_probing_ = f.probing;
}

void FramePacer::tick(TimePoint now) {
  if (in_flight_.empty()) return;
  TimePoint ref = in_flight_.front().sent;
  if (last_ack_ && *last_ack_ > ref) ref = *last_ack_;
  if (now - ref < config_.stall) return;
  stalls_++;
  stall_streak_++;
  late_ += in_flight_.size();
  in_flight_.clear();
  if (!answered_ || stall_streak_ >= config_.stalls_before_giving_up) {
    giveUp();
    return;
  }
  if (bandwidth_ > 0) bandwidth_ = std::max(config_.min_bandwidth, bandwidth_ / 2);
  samples_.clear();
  lag_ratios_.clear();
  delay_streak_ = 0;
  last_ack_.reset();
  last_ack_sent_.reset();
}

PacerStats FramePacer::stats(TimePoint now) const {
  PacerStats s;
  s.bandwidth = bandwidth_;
  s.rtt_min_us = rtt_known_ ? rtt_min_us_ : 0;
  size_t recent = 0;
  for (TimePoint t : recent_starts_) if (now - t <= std::chrono::seconds(1)) recent++;
  s.fps = static_cast<double>(recent);
  s.bytes_in_flight = inFlightBytes();
  for (const Fence & f : in_flight_) s.frames_in_flight += f.probe ? 0 : 1;
  s.last_frame = last_frame_;
  s.totals = totals_;
  s.frames = frames_;
  s.acks = acks_;
  s.backoffs = backoffs_;
  s.stalls = stalls_;
  s.fences = fences_;
  return s;
}

}  // namespace cellui::term
