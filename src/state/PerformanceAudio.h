#ifndef _PERFORMANCEAUDIO_H_
#define _PERFORMANCEAUDIO_H_

#include "../playback/PerformanceEffect.h"
#include "TrackCrush.h"
#include "TrackGate.h"
#include "TrackGrains.h"
#include "TrackShuffle.h"
#include "TrackStutter.h"

#include <algorithm>
#include <cstdint>

// What is held of the performance effects that work on the sound itself.
struct PerformanceAudioTargets {
  float stutter_rows = 0.0f; // 0: none
  bool stutter_start = false; // a new stutter, still to be started
  int stutter_delay = 0;      // frames to the row it starts capturing on
  float shuffle_rows = 0.0f;  // 0: none
  bool shuffle_start = false;
  int shuffle_delay = 0;
  float erode = -1.0f, shatter = -1.0f, crush = -1.0f; // negative: none
  float gate_rows = 0.0f;     // 0: none
  bool isIdle() const {
    return stutter_rows == 0.0f && shuffle_rows == 0.0f && erode < 0.0f && shatter < 0.0f && crush < 0.0f && gate_rows == 0.0f;
  }
};

// Where the song is, for the effects that follow its grid.
struct PerformanceGrid {
  int sample_rate = 44100;
  int regular_channels = 1;
  int row_frames = 1;
  int bar_frames = 16;
  int bar_phase = -1; // frames into the bar; negative while the transport is stopped
};

// The sound effects held on one track, or on the master: Stutter, Shuffle,
// Erode, Shatter, Crush and Gate, run in that order over its whole output.
class PerformanceAudio {
public:
  explicit PerformanceAudio(uint32_t seed)
    : shuffle_(seed * 3u + 1u), erode_(grains::kErode, seed * 3u + 2u), shatter_(grains::kShatter, seed * 3u + 3u) { }

  // Hands over what is held; clears the start flags it has acted on.
  void update(PerformanceAudioTargets & held, const PerformanceGrid & grid) {
    const int fade = static_cast<int>(performance::kStutterFadeSeconds * static_cast<float>(grid.sample_rate));
    const auto frames = [&](float rows) { return std::max(1, static_cast<int>(rows * static_cast<float>(grid.row_frames))); };

    if (held.stutter_start) {
      stutter_.start(held.stutter_delay, frames(held.stutter_rows), frames(performance::stutterSliceRows(0)), grid.regular_channels, fade);
    } else if (held.stutter_rows > 0.0f) {
      stutter_.setSlice(frames(held.stutter_rows));
    } else {
      stutter_.release();
    }
    held.stutter_start = false;

    if (held.shuffle_start) {
      const int capacity = std::min(grid.bar_frames, frames(static_cast<float>(performance::kShuffleMaxRows)));
      shuffle_.start(held.shuffle_delay, frames(held.shuffle_rows), capacity, grid.regular_channels, fade);
    } else if (held.shuffle_rows > 0.0f) {
      shuffle_.setSlice(frames(held.shuffle_rows));
    } else {
      shuffle_.release();
    }
    held.shuffle_start = false;

    setGrains(erode_, held.erode, grid);
    setGrains(shatter_, held.shatter, grid);

    if (held.crush < 0.0f) crush_.release();
    else if (crush_.isOff()) crush_.start(grid.sample_rate, held.crush);
    else crush_.setAmount(held.crush);

    if (held.gate_rows == 0.0f) {
      gate_.release();
    } else {
      const int period = frames(held.gate_rows);
      if (gate_.isOff()) gate_.start(period, grid.bar_phase, static_cast<int>(performance::kGateEdgeSeconds * static_cast<float>(grid.sample_rate)));
      else gate_.setPeriod(period, grid.bar_phase);
    }
  }

  bool isOff() const {
    return stutter_.isOff() && shuffle_.isOff() && erode_.isOff() && shatter_.isOff() && crush_.isOff() && gate_.isOff();
  }

  void process(AudioBuffer & data) {
    stutter_.process(data);
    shuffle_.process(data);
    erode_.process(data);
    shatter_.process(data);
    crush_.process(data);
    gate_.process(data);
  }

private:
  static void setGrains(TrackGrains & grains, float amount, const PerformanceGrid & grid) {
    if (amount < 0.0f) grains.release();
    else if (grains.isOff()) grains.start(grid.sample_rate, grid.regular_channels, amount);
    else grains.setAmount(amount);
  }

  TrackStutter stutter_;
  TrackShuffle shuffle_;
  TrackGrains erode_, shatter_;
  TrackCrush crush_;
  TrackGate gate_;
};

#endif
