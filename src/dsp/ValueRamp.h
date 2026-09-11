#ifndef _VALUERAMP_H_
#define _VALUERAMP_H_

#include <algorithm>

namespace dsp {

// A plain linear ramp from wherever it currently is toward a target,
// advanced in whole render-frame steps rather than continuously in real
// time - the audio-thread-side counterpart of what used to be a UI-thread
// timer ticking discrete Controller::setTrackSendA()/etc. calls (see
// LeafTrackState's own send_main_ramp_/send_a_ramp_/send_b_ramp_). Not
// tied to any particular unit (linear gain here, but nothing here assumes
// that) - glideTo()'s own caller is the one that knows what a frame count
// actually means for its own use.
class ValueRamp {
public:
  // Snaps to `value` immediately, discarding any glide in flight - what a
  // plain instant set (LeafTrackState::setSendMain()/etc., unrelated to a
  // glide at all) needs: an instant set arriving mid-glide should win
  // outright, not be raced by the glide's own next advance().
  void snapTo(float value) {
    current_ = value;
    target_ = value;
    frames_remaining_ = 0;
    per_frame_delta_ = 0.0f;
  }

  // Starts gliding from the current value toward `target`, reaching it
  // after `frames` frames (frames <= 0 behaves exactly like snapTo() -
  // there's nothing to spread a glide across otherwise).
  void glideTo(float target, int frames) {
    if (frames <= 0) {
      snapTo(target);
      return;
    }
    target_ = target;
    frames_remaining_ = frames;
    per_frame_delta_ = (target_ - current_) / static_cast<float>(frames);
  }

  // Advances by up to `frames` frames (never past the ramp's own
  // remaining length, so a caller doesn't need to clamp itself), returning
  // the value this ramp has reached by then. Snaps exactly to the target
  // once the ramp completes rather than approaching it asymptotically
  // through repeated float addition, so a long-running glide can't drift
  // off its true target from accumulated rounding error.
  float advance(int frames) {
    if (frames_remaining_ <= 0 || frames <= 0) return current_;
    int step = std::min(frames, frames_remaining_);
    current_ += per_frame_delta_ * static_cast<float>(step);
    frames_remaining_ -= step;
    if (frames_remaining_ <= 0) current_ = target_;
    return current_;
  }

  bool isActive() const { return frames_remaining_ > 0; }
  float getCurrentValue() const { return current_; }

private:
  float current_ = 0.0f;
  float target_ = 0.0f;
  float per_frame_delta_ = 0.0f;
  int frames_remaining_ = 0;
};

}

#endif
