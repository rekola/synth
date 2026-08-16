#include "FileInstrument.h"

#include "../audio/AudioBuffer.h"
#include "InstrumentVoice.h"

#include <sndfile.h>
#include <cstring>
#include <vector>

using namespace std;

#define BLOCK_SIZE 4096

bool
FileInstrument::openFile() {
  SNDFILE * infile = 0;
  SF_INFO sfinfo;

  memset(&sfinfo, 0, sizeof(sfinfo));

  if ((infile = sf_open(filename_.c_str(), SFM_READ, &sfinfo)) == NULL) {
    return false;
  }

  // printf("# Channels %d, Sample rate %d\n", sfinfo.channels, sfinfo.samplerate) ;
  int channels = sfinfo.channels;
  
  float * buf = (float *)malloc(BLOCK_SIZE * sizeof (float));
  if (buf == NULL) {
    return false;
  }
  
  sf_count_t frames = BLOCK_SIZE / channels;

  vector<float> buffer;
  int k, readcount;
  while ((readcount = (int) sf_readf_float (infile, buf, frames)) > 0) {
    for (k = 0 ; k < readcount * channels; k++) {
      buffer.push_back(buf[k]);
    }
  }

  free(buf);
  sf_close(infile);

  int total_frames = (int)buffer.size() / channels;
  samples_ = make_shared<AudioBuffer>(channels, total_frames);
  for (int c = 0; c < channels; c++) {
    auto out_buffer = samples_->getChannelData(c);
    for (int i = 0; i < total_frames; i++) {
      out_buffer[i] = buffer[static_cast<size_t>(i * channels + c)];
    }
  }
  return true;
}

class FileInstrumentVoice : public InstrumentVoice {
public:
  FileInstrumentVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, float detune, std::shared_ptr<AudioBuffer> samples, const SendLevels & sends = {}, const NoteCoordinate & note_coord = {})
    : InstrumentVoice(channel_config, position, detune, sends, note_coord), samples_(samples) { }

  AudioBuffer render(int frames) override {
    auto base_gain = decibelsToGain(getGainDB());

    // Sample files are always treated as mono, positioned like every other
    // leaf voice - encode it via this voice's own position
    // (InstrumentVoice::encodePosition()). A multi-channel file just uses
    // its first channel - there's no way to know the user's intent for
    // the remaining channels (a plain stereo sample? a pre-baked B-format
    // recording, where channel 0 happens to be W?), so no downmixing or
    // other guessing; a second, encodePosition()-bypassing code path to
    // treat a file's channels as pre-baked spatial content directly isn't
    // worth the complexity for how rare that case is. No getDistanceGain()
    // baked into dry_ here - encodePosition() applies distance attenuation
    // (and Send Main) itself now, see its own doc comment in
    // InstrumentVoice.h.
    if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));
    for (int k = 0; k < frames; k++) {
      int i = getSourceSamplePosition();
      stepForward(1);
      dry_[static_cast<size_t>(k)] = i >= samples_->size() ? 0.0f : samples_->getChannelData(0)[i] * base_gain;
    }
    return encodePosition(dry_.data(), frames);
  }

  void stopNote() override { sourceSamplePosition_ = samples_->size(); }
  bool isActive() const override { return sourceSamplePosition_ < samples_->size(); }
  void killNote() override { stopNote(); }

private:
  std::shared_ptr<AudioBuffer> samples_;
  std::vector<float> dry_;
};

std::unique_ptr<VoiceState>
FileInstrument::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  auto voice = std::make_unique<FileInstrumentVoice>(channel_config, position, detune, samples_, sends, note_coord);
  voice->playNote(frequency, velocity, note_value);
  return voice;
}
