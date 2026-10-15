#ifndef _NULLAUDIO_H_
#define _NULLAUDIO_H_

#include "AudioAPI.h"

#include <chrono>

// An output with no device: blocks are discarded, paced by the clock at the
// rate a real device would take them. Lets the real Player loop run where
// there is no sound hardware (CI, tests).
class NullAudio : public AudioAPI {
 public:
  NullAudio(int sample_rate, short channel_count, size_t block_frames = 1024) : AudioAPI(sample_rate, channel_count), block_frames_(block_frames) { }

  WaitResult wait(Notifier & wake, bool want_capture, int timeout_ms) override;
  void play(const AudioBuffer & data, Logger & logger) override;
  AudioBuffer record(Logger &) override;
  size_t getFrameCount() const override { return block_frames_; }
  void startRecording() override { }
  void stopRecording() override { }

  int getPlaybackDelayFrames() const override { return 0; }
  int getCaptureDelayFrames() const override { return 0; }

  bool hasCaptureDevice() const override { return false; }
  std::string getCaptureDeviceName() const override { return ""; }
  std::string getPlaybackDeviceName() const override { return "null"; }

  bool setCaptureDevice(const std::string &, Logger &) override { return false; }
  bool setPlaybackDevice(const std::string &, Logger &) override { return false; }

  // Blocks handed to play() so far.
  size_t blocksPlayed() const { return blocks_played_; }

 private:
  using Clock = std::chrono::steady_clock;

  size_t block_frames_;
  size_t blocks_played_ = 0;
  // When the next block is due; starts at the first wait().
  Clock::time_point next_block_;
  bool started_ = false;
};

#endif
