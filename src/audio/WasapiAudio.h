#ifndef _WASAPIAUDIO_H_
#define _WASAPIAUDIO_H_

#include "AudioAPI.h"
#include "DeviceSettings.h"

#include <memory>

struct WasapiStream;

// Windows audio through WASAPI: shared mode, event driven, 32-bit float.
// Output runs at the rate of the output device's mix format (adopted, as the
// ALSA backend adopts its negotiated rate); the system converts the channel
// count, and capture's rate and channel count, to ours.
class WasapiAudio : public AudioAPI {
 public:
  explicit WasapiAudio(int _freq, short _channels);
  ~WasapiAudio() override;

  // `devices` says what to open (see DeviceSettings.h). A named device that
  // is gone falls back to the default one, with a warning.
  void initialize(Logger & logger, const DeviceSettings & devices = DeviceSettings());

  WaitResult wait(Notifier & wake, bool want_capture, int timeout_ms) override;
  void play(const AudioBuffer & data, Logger & logger) override;
  AudioBuffer record(Logger & logger) override;
  size_t getFrameCount() const override { return block_frames_; }
  void startRecording() override;
  void stopRecording() override;

  int getPlaybackDelayFrames() const override;
  int getCaptureDelayFrames() const override;

  bool hasCaptureDevice() const override { return static_cast<bool>(capture_); }
  std::string getCaptureDeviceName() const override { return capture_name_; }
  std::string getPlaybackDeviceName() const override { return playback_name_; }

  bool setCaptureDevice(const std::string & name, Logger & logger) override;
  bool setPlaybackDevice(const std::string & name, Logger & logger) override;

 private:
  unsigned playbackFree() const;

  std::unique_ptr<WasapiStream> playback_, capture_;
  size_t block_frames_ = 0;
  bool capture_started_ = false;
  bool playback_failed_ = false, failure_logged_ = false;
  std::string playback_name_, capture_name_;
};

#endif
