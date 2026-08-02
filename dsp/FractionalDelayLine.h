#ifndef _FRACTIONALDELAYLINE_H_
#define _FRACTIONALDELAYLINE_H_

#include <vector>

// A circular buffer with linearly-interpolated fractional-delay reads:
// write one sample per call, then read back at any delay (including
// non-integer sample counts) up to the buffer's own length. Extracted
// from ChorusEngine's own per-channel circular buffer (its LFO-modulated
// multi-voice tap is still ChorusEngine's own logic - this class is just
// the shared write/read primitive underneath it), so any other consumer
// needing a single delayed copy of a signal (geometry-derived or
// otherwise) can reuse the same tested read/write math instead of a
// second hand-rolled copy.
class FractionalDelayLine {
 public:
  FractionalDelayLine() = default;
  explicit FractionalDelayLine(int bufferLength) { resize(bufferLength); }

  // Resets the buffer (and write position) to `bufferLength` samples of
  // silence - callers size this to their own maximum possible delay plus
  // a little margin for the interpolation's +1 read.
  void resize(int bufferLength) {
    buffer_.assign(static_cast<size_t>(bufferLength), 0.0f);
    write_pos_ = 0;
  }

  void write(float sample) {
    buffer_[static_cast<size_t>(write_pos_)] = sample;
    write_pos_++;
    if (write_pos_ >= static_cast<int>(buffer_.size())) write_pos_ = 0;
  }

  // Reads `delaySamples` behind the most recently written sample (delay
  // 0 returns exactly that sample), linearly interpolated between the
  // two nearest integer sample positions. `delaySamples` should stay
  // within the buffer's own length (minus a little margin) - this does
  // not itself enforce that bound, same as the code it was extracted
  // from.
  float read(float delaySamples) const {
    int bufLen = static_cast<int>(buffer_.size());
    float read_pos = static_cast<float>(write_pos_ - 1) - delaySamples;
    if (read_pos < 0.0f) read_pos += static_cast<float>(bufLen);

    int i0 = static_cast<int>(read_pos);
    float frac = read_pos - static_cast<float>(i0);
    if (i0 >= bufLen) i0 -= bufLen;
    int i1 = i0 + 1;
    if (i1 >= bufLen) i1 -= bufLen;

    float s0 = buffer_[static_cast<size_t>(i0)];
    float s1 = buffer_[static_cast<size_t>(i1)];
    return s0 + (s1 - s0) * frac;
  }

 private:
  std::vector<float> buffer_;
  int write_pos_ = 0;
};

#endif
