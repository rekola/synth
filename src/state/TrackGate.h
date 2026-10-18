#ifndef _TRACKGATE_H_
#define _TRACKGATE_H_

#include "../audio/AudioBuffer.h"

#include <algorithm>

// Gate (PerformanceEffect::GATE): a track's output (or the mix) chopped on
// and off on the row grid while held - open for the first half of each
// period, shut for the second - with short edges. The sound underneath
// carries on, so it comes back wherever the song has got to.
class TrackGate {
 public:
  // Starts gating with `period` frames, the grid `phase` frames into one
  // (negative when there is no grid to follow: the transport is stopped).
  void start(int period, int phase, int edge) {
    edge_ = std::max(1, edge);
    gain_ = 1.0f;
    pos_ = 0;
    active_ = true;
    releasing_ = false;
    setPeriod(period, phase);
  }

  // A new period, and where the grid is in it (negative: keep counting).
  void setPeriod(int period, int phase) {
    releasing_ = false; // takes back a release still fading out
    period_ = std::max(2, period);
    if (phase >= 0)
      pos_ = phase % period_;
    else
      pos_ %= period_;
  }

  void release() { releasing_ = true; }
  bool isOff() const { return !active_; }

  void process(AudioBuffer & data) {
    if (!active_) return;
    const int frames = data.numberOfFrames();
    const float step = 1.0f / static_cast<float>(edge_);
    const int channels = data.numberOfChannels();
    for (int f = 0; f < frames; f++) {
      const bool open = releasing_ || pos_ < period_ / 2;
      gain_ = open ? std::min(1.0f, gain_ + step) : std::max(0.0f, gain_ - step);
      for (int c = 0; c < channels; c++) data.getChannelData(c)[f] *= gain_;
      if (++pos_ >= period_) pos_ = 0;
    }
    if (releasing_ && gain_ >= 1.0f) active_ = false;
  }

 private:
  int period_ = 2, pos_ = 0, edge_ = 1;
  float gain_ = 1.0f;
  bool active_ = false, releasing_ = false;
};

#endif
