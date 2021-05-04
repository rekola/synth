#include "FileInstrument.h"

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
  FileInstrumentVoice(int _identifier, std::shared_ptr<SampleData> _samples) : InstrumentVoice(_identifier), samples(_samples) { }

  void render(float * buffer, size_t frames, size_t offset) override {
    float gain = decibelsToGain(getGainDB());

    bool ended = false;
    for (size_t k = 0; k < frames; k++) {
      // float i = getFphase() * WAVESIZE / 44100.0f;
      size_t i = (size_t)getWavePosition();
      stepForward();

      float s;
      if (i < samples->size()) {
	s = samples->data()[i];
      } else {
	s = 0.0f;
	ended = true;
      }

      buffer[k + offset] = s * gain;
    }
    
    if (ended) killNote();
  }
  
private:
  std::shared_ptr<SampleData> samples;
};

std::shared_ptr<InstrumentVoice>
FileInstrument::createVoice(int _identifier) const {
  return std::make_shared<FileInstrumentVoice>(_identifier, samples);
}
