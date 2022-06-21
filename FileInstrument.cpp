#include "FileInstrument.h"

#include "SampleData.h"
#include "InstrumentVoice.h"

#include <sndfile.h>
#include <cstring>

using namespace std;

#define BLOCK_SIZE 4096

void
FileInstrument::openFile() {
  SNDFILE * infile = 0;
  SF_INFO sfinfo;

  memset(&sfinfo, 0, sizeof(sfinfo));

  if ((infile = sf_open(filename.c_str(), SFM_READ, &sfinfo)) == NULL) {
    // printf ("Not able to open input file %s.\n", filename.c_str());
    // puts (sf_strerror (NULL)) ;
    return;
  }

  // printf("# Channels %d, Sample rate %d\n", sfinfo.channels, sfinfo.samplerate) ;
  int channels = sfinfo.channels;
  
  float * buf = (float *)malloc(BLOCK_SIZE * sizeof (float));
  if (buf == NULL) {
    // printf ("Error : Out of memory.\n\n") ;
    return;
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

  samples = make_shared<SampleData>(channels, buffer.size());
  auto out_buffer = samples->data();
  for (int i = 0; i < samples->size(); i++) {
    out_buffer[i] = buffer[i];
  }
}

class FileInstrumentVoice : public InstrumentVoice {
public:
  FileInstrumentVoice(const ChannelConfiguration & _channel_config, float _azimuth, std::shared_ptr<SampleData> _samples)
    : InstrumentVoice(_channel_config, _azimuth), samples(_samples) { }

  SampleData render(int frames) override {
    auto gain = decibelsToGain(getGainDB());

    SampleData output(getChannelConfiguration(), frames);
    auto outChannels = output.numberOfChannels();
    auto buffer = output.data();

    auto inChannels = samples->numberOfChannels();
    
    for (int k = 0; k < frames; k++) {
      // float i = getFphase() * WAVESIZE / getOutSampleRate();
      int i = getSourceSamplePosition();
      stepForward(1);

      if (i >= samples->size()) {
	for (auto l = 0; l < outChannels; l++) {
	  buffer[outChannels * k + l] = 0.0f;
	}
      } else if (outChannels == inChannels) {	
	for (auto l = 0; l < outChannels; l++) {
	  buffer[outChannels * k + l] = samples->data()[i * inChannels + l] * gain;
	}
      } else if (inChannels == 1) {
	for (auto l = 0; l < outChannels; l++) {
	  buffer[outChannels * k + l] = samples->data()[i] * gain;
	}
      } else {
	assert(0);
      }
    }
        
    return output;
  }

  void stopNote() override { sourceSamplePosition = samples->size(); }
  bool isPlaying() const override { return sourceSamplePosition < samples->size(); }
  void killNote() override { stopNote(); }

private:
  std::shared_ptr<SampleData> samples;
};

std::unique_ptr<TrackState>
FileInstrument::playNote(const ChannelConfiguration & channel_config, float azimuth, float frequency, float velocity, float start_phase) const {
  auto voice = std::make_unique<FileInstrumentVoice>(channel_config, azimuth, samples);
  voice->playNote(frequency, velocity, start_phase);
  return voice;
}
