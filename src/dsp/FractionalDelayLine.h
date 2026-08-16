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

  // 4-point, 3rd-order Lagrange interpolation - noticeably more accurate
  // than read()'s linear interpolation for a *moving* delaySamples, which
  // is exactly what a modulated (wow/flutter-style) read needs: linear
  // interpolation's high-frequency aliasing becomes audible precisely
  // because the read pointer never sits still. Needs two extra samples of
  // history versus read()'s single interpolated pair (one sample before
  // i0, one past i0+1) - resize() callers using this method should size
  // their buffer for their max delay plus at least 2, not +1.
  float readCubic(float delaySamples) const {
    int bufLen = static_cast<int>(buffer_.size());
    float read_pos = static_cast<float>(write_pos_ - 1) - delaySamples;
    if (read_pos < 0.0f) read_pos += static_cast<float>(bufLen);

    int i0 = static_cast<int>(read_pos);
    float frac = read_pos - static_cast<float>(i0);
    if (i0 >= bufLen) i0 -= bufLen;

    int im1 = i0 - 1;
    if (im1 < 0) im1 += bufLen;
    int i1 = i0 + 1;
    if (i1 >= bufLen) i1 -= bufLen;
    int i2 = i0 + 2;
    if (i2 >= bufLen) i2 -= bufLen;

    float ym1 = buffer_[static_cast<size_t>(im1)];
    float y0 = buffer_[static_cast<size_t>(i0)];
    float y1 = buffer_[static_cast<size_t>(i1)];
    float y2 = buffer_[static_cast<size_t>(i2)];

    // Lagrange basis weights for nodes at x = -1, 0, 1, 2, evaluated at
    // x = frac (0 <= frac < 1) - see e.g. the "4-point, 3rd-order
    // Lagrange" entry on musicdsp.org's interpolation page for the same
    // derivation.
    float wm1 = -frac * (frac - 1.0f) * (frac - 2.0f) * (1.0f / 6.0f);
    float w0 = (frac + 1.0f) * (frac - 1.0f) * (frac - 2.0f) * 0.5f;
    float w1 = -(frac + 1.0f) * frac * (frac - 2.0f) * 0.5f;
    float w2 = (frac + 1.0f) * frac * (frac - 1.0f) * (1.0f / 6.0f);

    return wm1 * ym1 + w0 * y0 + w1 * y1 + w2 * y2;
  }

 private:
  std::vector<float> buffer_;
  int write_pos_ = 0;
};

#endif
