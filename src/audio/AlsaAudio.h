#ifndef _ALSAAUDIO_H_
#define _ALSAAUDIO_H_

#include "AudioAPI.h"
#include "DeviceSettings.h"

#include <alsa/asoundlib.h>
#include <chrono>

class AlsaAudio : public AudioAPI {
 public:
  explicit AlsaAudio(int _freq, short _channels) : AudioAPI(_freq, _channels) { }
  ~AlsaAudio();

  // `devices` says what to open (see DeviceSettings.h); "default" otherwise.
  // A named device that can't be opened or isn't there falls back to the
  // system default, with a warning - a stale saved choice (an unplugged
  // interface) must not leave the program without sound.
  void initialize(Logger & logger, const DeviceSettings & devices = DeviceSettings());

  WaitResult wait(Notifier & wake, bool want_capture, int timeout_ms) override;
  void play(const AudioBuffer & data, Logger & logger) override;
  AudioBuffer record(Logger & logger) override;
  size_t getFrameCount() const override { return output_frames; }
  void startRecording() override;
  void stopRecording() override;

  int getPlaybackDelayFrames() const override;
  int getCaptureDelayFrames() const override;

  bool hasCaptureDevice() const override { return capture_handle != nullptr; }
  std::string getCaptureDeviceName() const override { return capture_device_name_; }
  std::string getPlaybackDeviceName() const override { return playback_device_name_; }

  bool setCaptureDevice(const std::string & name, Logger & logger) override;
  bool setPlaybackDevice(const std::string & name, Logger & logger) override;

 private:
  std::vector<pollfd> getPollDescriptors(snd_pcm_t * handle);

  // Opens and configures one direction at the rate/channel count in use,
  // returning its period size (0 on failure, already logged). Playback's
  // result is checked against output_frames by the caller where it matters.
  size_t openCapture(const std::string & name, Logger & logger, snd_pcm_t *& handle);
  // The streams' poll descriptors and wait()'s scratch set.
  std::vector<pollfd> playback_fds_, capture_fds_, wait_fds_;

  snd_pcm_t * pcm_handle = 0, * capture_handle = 0;
  size_t output_frames = 0, input_frames = 0;
  bool recording_started = false;
  // Last playback write error and when it was logged (play()'s own rate limit).
  int last_play_error_ = 0;
  std::chrono::steady_clock::time_point last_play_error_log_;
  // The name capture actually opened with, if it did - getCaptureDeviceName()'s
  // own backing store. Set once, in initialize(), regardless of success -
  // only meaningful (and only ever read) when capture_handle is non-null.
  std::string capture_device_name_;
  // Likewise for playback; "default" after a fallback.
  std::string playback_device_name_;
};

#endif
