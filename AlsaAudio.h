#ifndef _ALSAAUDIO_H_
#define _ALSAAUDIO_H_

#include "AudioAPI.h"

#include <alsa/asoundlib.h>

class AlsaAudio : public AudioAPI {
 public:
  explicit AlsaAudio(int _freq, short _channels) : AudioAPI(_freq, _channels) { }
  ~AlsaAudio();

  void initialize(Logger & logger);

  void play(const SampleData & data, Logger & logger) override;
  SampleData record(Logger & logger) override;
  size_t getFrameCount() const override { return output_frames; }
  void startRecording() override;
  void stopRecording() override;  
  std::vector<MidiEvent> recordMIDI() override;

private:
  std::vector<pollfd> getPollDescriptors(snd_pcm_t * handle);
  std::vector<pollfd> getMidiPollDescriptors(snd_seq_t * handle);
  
  snd_pcm_t * pcm_handle = 0, * capture_handle = 0;
  snd_seq_t * seq_handle = 0;
  size_t output_frames = 0, input_frames = 0;
  bool recording_started = false;
};

#endif
