#ifndef _RECORDINGRINGBUFFER_H_
#define _RECORDINGRINGBUFFER_H_

#include "../audio/AudioBuffer.h"

#include <vector>

// A fixed-capacity mono pre-roll buffer for loudness-threshold-triggered
// recording: while armed and waiting for input to cross the trigger
// threshold, every captured block is pushed in here so the moment it
// actually does, drain() can splice the already-captured lead-in onto the
// front of the take, recovering an attack's own rising edge instead of
// starting the clip right at the trigger and truncating it. Genuinely
// circular (unlike dsp/SpectrumAnalyzer.h's own shift-buffer accumulator,
// which only ever needs "the last N samples" for one always-full analysis
// window, never a chronological drain of however much has actually been
// captured so far) - a real ring, overwriting its own oldest content once
// full rather than shifting everything down on every push.
class RecordingRingBuffer {
 public:
  // `capacity_frames` is fixed for this instance's own lifetime - Player
  // owns one, sized once from a compiled duration constant, never resized
  // at runtime.
  explicit RecordingRingBuffer(int capacity_frames) : buffer_(static_cast<size_t>(capacity_frames < 0 ? 0 : capacity_frames)) { }

  // Copies `block`'s own Main channel in, one push per captured audio
  // block (arbitrary size, not necessarily capacity-sized) - overwrites
  // the oldest still-buffered content once full, same as any ring.
  void push(const AudioBuffer & block) {
    if (buffer_.empty()) return;
    auto data = block.getChannel(Channel::Main);
    if (!data) return;
    auto capacity = static_cast<int>(buffer_.size());
    for (int i = 0; i < block.numberOfFrames(); i++) {
      buffer_[static_cast<size_t>(write_pos_)] = data[i];
      write_pos_ = (write_pos_ + 1) % capacity;
      if (valid_frames_ < capacity) valid_frames_++;
    }
  }

  // How many frames have actually ever been pushed since the last
  // reset()/construction, capped at capacity - armed for less time than
  // this buffer's own length still drains correctly, just shorter than a
  // full capacity's worth.
  int validFrames() const { return valid_frames_; }

  // Returns exactly validFrames() samples, oldest first (chronological
  // order, ready to prepend onto a take as-is) - "drain", not "peek":
  // this content is meant to be consumed exactly once per trigger, so
  // this also clears back to empty (as reset() does) rather than leaving
  // stale content a later, unrelated trigger could see again.
  AudioBuffer drain() {
    AudioBuffer result(1, valid_frames_);
    if (valid_frames_ > 0) {
      auto capacity = static_cast<int>(buffer_.size());
      // The oldest sample currently held is capacity behind write_pos_ (or,
      // while not yet full, simply index 0 - write_pos_ hasn't wrapped
      // yet, so everything from 0 up to write_pos_ is already in
      // chronological order).
      auto start = valid_frames_ < capacity ? 0 : write_pos_;
      auto dst = result.getChannelData(0);
      for (int i = 0; i < valid_frames_; i++) {
        dst[i] = buffer_[static_cast<size_t>((start + i) % capacity)];
      }
    }
    reset();
    return result;
  }

  // Clears back to empty without touching capacity - a fresh arm cycle
  // must never let an earlier, temporally-discontinuous cycle's own
  // leftover content sit in the ring for a later drain() to splice in
  // alongside genuinely fresh audio.
  void reset() {
    valid_frames_ = 0;
    write_pos_ = 0;
  }

 private:
  std::vector<float> buffer_;
  int write_pos_ = 0;
  int valid_frames_ = 0;
};

#endif
