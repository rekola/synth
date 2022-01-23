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
      buffer.push_back(buf[k * channels + 0]);
    }
  }

  free(buf);
  sf_close(infile);

  samples = make_shared<SampleData>(1, buffer.size());
  auto out_buffer = samples->data();
  for (size_t i = 0; i < samples->size(); i++) {
    out_buffer[i] = buffer[i];
  }
}

class FileInstrumentVoice : public InstrumentVoice {
public:
  FileInstrumentVoice(ChannelConfiguration _channel_config, unsigned int _outSampleRate, float _azimuth, std::shared_ptr<SampleData> _samples)
    : InstrumentVoice(_channel_config, _outSampleRate, _azimuth), samples(_samples) { }

  SampleData render(size_t frames) override {
    float gain = decibelsToGain(getGainDB());

    SampleData output(1, frames);
    auto buffer = output.data();
    
    for (size_t k = 0; k < frames; k++) {
      // float i = getFphase() * WAVESIZE / getOutSampleRate();
      size_t i = (size_t)getSourceSamplePosition();
      stepForward(1);

      float s;
      if (i < samples->size()) {
	s = samples->data()[i];
      } else {
	s = 0.0f;
      }

      buffer[k] += s * gain;
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
FileInstrument::playNote(ChannelConfiguration channel_config, unsigned int outSampleRate, float azimuth, float frequency, float velocity, float start_phase) const {
  auto voice = std::make_unique<FileInstrumentVoice>(channel_config, outSampleRate, azimuth, samples);
  voice->playNote(frequency, velocity, start_phase);
  return voice;
}
