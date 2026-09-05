#ifndef _ALSAAUDIO_H_
#define _ALSAAUDIO_H_

#include "AudioAPI.h"

#include <alsa/asoundlib.h>

class AlsaAudio : public AudioAPI {
 public:
  explicit AlsaAudio(int _freq, short _channels) : AudioAPI(_freq, _channels) { }
  ~AlsaAudio();

  // `capture_device_name` overrides the ALSA PCM name capture opens
  // ("default" otherwise) - main.cpp's own --capture-device flag, for a
  // machine where ALSA/PipeWire's own "default" doesn't resolve to the
  // input the player actually wants (see this class's own comment on
  // capture_device_name_).
  void initialize(Logger & logger, std::string capture_device_name = "default");

  void play(const AudioBuffer & data, Logger & logger) override;
  AudioBuffer record(Logger & logger) override;
  size_t getFrameCount() const override { return output_frames; }
  void startRecording() override;
  void stopRecording() override;
  std::vector<MidiEvent> recordMIDI() override;

  int getPlaybackDelayFrames() const override;
  int getCaptureDelayFrames() const override;

  bool hasCaptureDevice() const override { return capture_handle != nullptr; }
  std::string getCaptureDeviceName() const override { return capture_device_name_; }

private:
  std::vector<pollfd> getPollDescriptors(snd_pcm_t * handle);
  std::vector<pollfd> getMidiPollDescriptors(snd_seq_t * handle);
  
  snd_pcm_t * pcm_handle = 0, * capture_handle = 0;
  snd_seq_t * seq_handle = 0;
  size_t output_frames = 0, input_frames = 0;
  bool recording_started = false;
  // The name capture actually opened with, if it did - getCaptureDeviceName()'s
  // own backing store. Set once, in initialize(), regardless of success -
  // only meaningful (and only ever read) when capture_handle is non-null.
  std::string capture_device_name_;
};

#endif
