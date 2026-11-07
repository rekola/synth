#include <cellui/term/FramePacer.h>

#include <algorithm>
#include <vector>

namespace cellui::term {

namespace {

using std::chrono::duration;
using std::chrono::duration_cast;

double seconds(FramePacer::TimePoint later, FramePacer::TimePoint earlier) { return duration<double>(later - earlier).count(); }

double seconds_of(std::chrono::microseconds d) { return static_cast<double>(d.count()) / 1e6; }

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
  double cap = std::max(config_.in_flight_bdp * bandwidth_ * delayUs() * 1e-6,
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
    for (const Fence & f : in_flight_) frames += f.lead ? 0 : 1;
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
  return gap > floor * config_.link_bound_margin && now < plus(*last_start_, gap);
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

void FramePacer::onLeadSent(TimePoint t_sent) {
  if (!fences_) return;
  Fence f;
  f.lead = true;
  f.sent = t_sent;
  in_flight_.push_back(f);
}

void FramePacer::onFrameSent(FrameBytes size, TimePoint t_sent, size_t lead_bytes) {
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
    f.lead_bytes = std::min(lead_bytes, size.total());
    f.started = pending_start_;
    f.sent = t_sent;
    f.in_flight_at_send = inFlightBytes() + size.total();
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

double FramePacer::delayUs() const {
  double best = 0;
  for (const auto & [t, d] : delays_) best = best == 0 ? d : std::min(best, d);
  return best;
}

double FramePacer::typicalDelayUs() const {
  if (delays_.empty()) return 0;
  std::vector<double> v;
  v.reserve(delays_.size());
  for (const auto & [t, d] : delays_) v.push_back(d);
  size_t k = std::min(v.size() - 1, static_cast<size_t>(config_.delay_quantile * static_cast<double>(v.size())));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return v[k];
}

void FramePacer::onAck(TimePoint t_ack) {
  if (in_flight_.empty()) return;
  Fence f = in_flight_.front();
  in_flight_.pop_front();
  acks_++;
  answered_ = true;
  stall_streak_ = 0;
  auto micros = [](TimePoint a, TimePoint b) { return duration<double, std::micro>(a - b).count(); };

  if (f.lead) {
    // How long the terminal takes to answer with nothing in front of it: now, not at start-up.
    delays_.push_back({t_ack, std::max(1.0, micros(t_ack, f.sent))});
    while (!delays_.empty() && t_ack - delays_.front().first > config_.delay_window) delays_.pop_front();
    lead_ack_ = t_ack;
    last_ack_ = t_ack;
    return;
  }
  frame_acks_++;
  last_payload_ = f.size.total() - f.lead_bytes;
  double payload = static_cast<double>(f.size.total() - f.lead_bytes);
  double floor_s = seconds_of(config_.sample_floor);
  // The frame's own time: from the reply to the fence in front of it to the reply to the one behind it.
  // Replies read together share a time and give none, which is no sample, never a bandwidth.
  double own = lead_ack_ && t_ack > *lead_ack_ ? seconds(t_ack, *lead_ack_) : 0;
  lead_ack_kept_ = lead_ack_ ? *lead_ack_ : t_ack;
  lead_ack_.reset();
  bool valid = own >= floor_s;
  // A frame whose own time is under the floor was taken faster than the floor says: the link does at
  // least a floor interval's worth of it. That bounds the estimate from below (it never lowers it).
  double bound = payload > 0 ? payload / std::max(own, seconds_of(config_.min_frame_interval)) : 0;
  last_sample_ = 0;
  last_grade_ = 0;
  if (bandwidth_ <= 0) {
    // The first frame: the first estimate, the next ones raise it or correct it.
    double first = valid ? payload / own : bound;
    if (first > 0) {
      bandwidth_ = first;
      last_sample_ = first;
      last_grade_ = valid ? 1 : 0;
    }
  } else {
    double d_us = typicalDelayUs();
    // Saturation-grade: the pipe was full when it was sent, so the frame's time is the link's. Probe-grade:
    // sent faster than the estimate, and big enough that its time at the estimate is above the floor.
    double floor_bytes = std::min(static_cast<double>(config_.grade_floor_bytes), config_.saturation_fraction * bandwidth_ * floor_s);
    double grade_bytes = std::max(config_.saturation_fraction * bandwidth_ * d_us * 1e-6, floor_bytes);
    bool saturated = d_us > 0 && static_cast<double>(f.in_flight_at_send) >= grade_bytes;
    bool probe_grade = f.probing && payload / bandwidth_ >= floor_s;
    bool may_lower = saturated || probe_grade;
    // A saturated frame is timed with the ones just before it, as long as the pipe was never empty
    // between them (each was started before the reply to the one before): from the reply before the run
    // to this one. After an idle spell the time starts at the reply to the run's first lead fence, so no
    // idle time is ever in a sample.
    double span = own, bytes = payload;
    if (saturated) {
      size_t run = 1, idx = acked_.size();
      TimePoint next_started = f.started, first_lead = lead_ack_kept_;
      while (idx > 0 && run < config_.sample_frames) {
        const Acked & prev = acked_[idx - 1];
        if (!(prev.saturated && next_started <= prev.ack)) break;
        bytes += prev.payload;
        first_lead = prev.lead_ack;
        next_started = prev.started;
        run++;
        idx--;
      }
      TimePoint start = (idx > 0 && next_started <= acked_[idx - 1].ack) ? acked_[idx - 1].ack : first_lead;
      span = seconds(t_ack, start);
    }
    valid = span >= floor_s;
    last_grade_ = valid ? (saturated ? 2 : probe_grade ? 3 : 1) : 0;
    if (valid) {
      double sample = std::min(bytes / span, config_.sample_cap * bandwidth_);
      last_sample_ = sample;
      samples_.push_back({sample, may_lower});
      while (samples_.size() > config_.sample_window) samples_.pop_front();
      double raise = 0, lower = 0;
      for (const Sample & x : samples_) {
        raise = std::max(raise, x.rate);
        if (x.may_lower) lower = std::max(lower, x.rate);
      }
      if (raise >= bandwidth_) {
        bandwidth_ = raise;  // any sample may raise it
      } else if (may_lower && lower > 0) {
        bandwidth_ += config_.down_weight * (std::min(lower, bandwidth_) - bandwidth_);  // only a grade one lowers it
      }
    }
    if (!valid && bound > bandwidth_) bandwidth_ = std::min(bound, config_.sample_cap * bandwidth_);
    // Queueing: later than the terminal's delay, the transfer and a slack. Only a frame that could have
    // been queued counts either way.
    double slack = std::max(config_.queue_slack_d * d_us, static_cast<double>(config_.queue_slack_floor.count()));
    bool queueing = d_us > 0 && micros(t_ack, f.sent) > d_us + payload / bandwidth_ * 1e6 + slack;
    if (may_lower && queueing) delay_streak_++;
    else if (!queueing) delay_streak_ = 0;
    if (delay_streak_ >= config_.delay_frames) {
      backOff();
    } else if (may_lower && last_end_ack_ && last_end_started_) {
      // The replies further apart than the frames were sent: the link is the slower.
      double gap1 = seconds(t_ack, *last_end_ack_);
      double send_gap = seconds(f.started, *last_end_started_);
      if (!f.probing && !last_end_probing_ && send_gap > 1e-5 && gap1 > 1e-5) {
        lag_ratios_.push_back(gap1 / send_gap);
        while (lag_ratios_.size() > static_cast<size_t>(config_.lag_frames)) lag_ratios_.pop_front();
        if (lag_ratios_.size() == static_cast<size_t>(config_.lag_frames) &&
            std::all_of(lag_ratios_.begin(), lag_ratios_.end(), [&](double r) { return r > config_.lag_ratio; })) {
          backOff();
        }
      }
    }
  }
  if (bandwidth_ > 0) bandwidth_ = std::max(bandwidth_, config_.min_bandwidth);
  if (config_.max_bandwidth > 0) bandwidth_ = std::min(bandwidth_, config_.max_bandwidth);
  last_ack_ = t_ack;
  acked_.push_back({t_ack, lead_ack_kept_, f.started, payload, last_grade_ == 2});
  while (acked_.size() > config_.sample_frames + 1) acked_.pop_front();
  last_end_ack_ = t_ack;
  last_end_started_ = f.started;
  last_end_probing_ = f.probing;
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
  last_end_ack_.reset();
  last_end_started_.reset();
  lead_ack_.reset();
}

PacerStats FramePacer::stats(TimePoint now) const {
  PacerStats s;
  s.bandwidth = bandwidth_;
  s.rtt_min_us = delayUs();
  s.delay_typ_us = typicalDelayUs();
  s.last_sample = last_sample_;
  s.last_payload = last_payload_;
  s.last_grade = last_grade_;
  size_t recent = 0;
  for (TimePoint t : recent_starts_) if (now - t <= std::chrono::seconds(1)) recent++;
  s.fps = static_cast<double>(recent);
  s.bytes_in_flight = inFlightBytes();
  for (const Fence & f : in_flight_) s.frames_in_flight += f.lead ? 0 : 1;
  s.last_frame = last_frame_;
  s.floor_rate = static_cast<double>(last_frame_.total()) / duration<double>(config_.min_frame_interval).count();
  s.totals = totals_;
  s.frames = frames_;
  s.acks = acks_;
  s.frame_acks = frame_acks_;
  s.backoffs = backoffs_;
  s.stalls = stalls_;
  s.fences = fences_;
  return s;
}

}  // namespace cellui::term
