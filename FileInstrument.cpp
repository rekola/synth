#include "FileInstrument.h"

#include "SampleData.h"
#include "InstrumentVoice.h"

#include <sndfile.h>
#include <cstring>

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
    for (k = 0 ; k < readcount; k++) {
      buffer.push_back(buf[k]);
    }
  }

  free(buf);
  sf_close(infile);

  samples_ = make_shared<SampleData>(channels, buffer.size());
  auto out_buffer = samples_->data();
  for (int i = 0; i < samples_->size(); i++) {
    out_buffer[i] = buffer[i];
  }

  return true;
}

class FileInstrumentVoice : public InstrumentVoice {
public:
  FileInstrumentVoice(const ChannelConfiguration & channel_config, float azimuth, float detune, float start_phase, std::shared_ptr<SampleData> samples)
    : InstrumentVoice(channel_config, azimuth, detune, start_phase), samples_(samples) { }

  SampleData render(int frames) override {
    auto gain = decibelsToGain(getGainDB());

    SampleData output(getChannelConfiguration(), frames);
    auto outChannels = output.numberOfChannels();
    auto buffer = output.data();

    auto inChannels = samples_->numberOfChannels();
    
    for (int k = 0; k < frames; k++) {
      // float i = getFphase() * WAVESIZE / getOutSampleRate();
      int i = getSourceSamplePosition();
      stepForward(1);

      if (i >= samples_->size()) {
	for (auto l = 0; l < outChannels; l++) {
	  buffer[outChannels * k + l] = 0.0f;
	}
      } else if (outChannels == inChannels) {	
	for (auto l = 0; l < outChannels; l++) {
	  buffer[outChannels * k + l] = samples_->data()[i * inChannels + l] * gain;
	}
      } else if (inChannels == 1) {
	for (auto l = 0; l < outChannels; l++) {
	  buffer[outChannels * k + l] = samples_->data()[i] * gain;
	}
      } else {
	assert(0);
      }
    }
        
    return output;
  }

  void stopNote() override { sourceSamplePosition_ = samples_->size(); }
  bool isPlaying() const override { return sourceSamplePosition_ < samples_->size(); }
  void killNote() override { stopNote(); }

private:
  std::shared_ptr<SampleData> samples_;
};

std::unique_ptr<TrackState>
FileInstrument::playNote(const ChannelConfiguration & channel_config, float azimuth, float frequency, float detune, float velocity, float start_phase) const {
  auto voice = std::make_unique<FileInstrumentVoice>(channel_config, azimuth, detune, start_phase, samples_);
  voice->playNote(frequency, velocity);
  return voice;
}
