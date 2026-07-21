#include "FileInstrument.h"

#include "SampleData.h"
#include "InstrumentVoice.h"
#include "AmbisonicEncoding.h"

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
    for (k = 0 ; k < readcount * channels; k++) {
      buffer.push_back(buf[k]);
    }
  }

  free(buf);
  sf_close(infile);

  int total_frames = (int)buffer.size() / channels;
  samples_ = make_shared<SampleData>(channels, total_frames);
  for (int c = 0; c < channels; c++) {
    auto out_buffer = samples_->getChannelData(c);
    for (int i = 0; i < total_frames; i++) {
      out_buffer[i] = buffer[i * channels + c];
    }
  }
  samples_->setNonZero();

  return true;
}

class FileInstrumentVoice : public InstrumentVoice {
public:
  FileInstrumentVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, float detune, float start_phase, std::shared_ptr<SampleData> samples, float send_a = 0.0f, float send_b = 0.0f)
    : InstrumentVoice(channel_config, position, detune, start_phase, send_a, send_b), samples_(samples) { }

  SampleData render(int frames) override {
    // base_gain (undistanced) feeds the sends - see InstrumentVoice::getDistanceGain().
    auto base_gain = decibelsToGain(getGainDB());
    auto gain = base_gain * getDistanceGain();

    auto channels = regularChannelsFor(getChannelConfiguration());
    if (getSendA() > 0.0f) channels.push_back(Channel::SendA);
    if (getSendB() > 0.0f) channels.push_back(Channel::SendB);

    SampleData output(channels, frames);
    auto outChannels = getChannelConfiguration().numberOfChannels();
    auto inChannels = samples_->numberOfChannels();
    auto send_a = output.getChannel(Channel::SendA);
    auto send_b = output.getChannel(Channel::SendB);

    for (int k = 0; k < frames; k++) {
      // float i = getFphase() * WAVESIZE / getOutSampleRate();
      int i = getSourceSamplePosition();
      stepForward(1);

      float raw0 = 0.0f;
      if (i >= samples_->size()) {
	for (auto l = 0; l < outChannels; l++) {
	  output.getChannelData(l)[k] = 0.0f;
	}
      } else if (outChannels == inChannels) {
	for (auto l = 0; l < outChannels; l++) {
	  output.getChannelData(l)[k] = samples_->getChannelData(l)[i] * gain;
	}
	raw0 = samples_->getChannelData(0)[i];
      } else if (inChannels == 1) {
	for (auto l = 0; l < outChannels; l++) {
	  output.getChannelData(l)[k] = samples_->getChannelData(0)[i] * gain;
	}
	raw0 = samples_->getChannelData(0)[i];
      } else {
	assert(0);
      }

      if (send_a) send_a[k] = raw0 * base_gain * getSendA();
      if (send_b) send_b[k] = raw0 * base_gain * getSendB();
    }

    output.setNonZero();
    return output;
  }

  void stopNote() override { sourceSamplePosition_ = samples_->size(); }
  bool isActive() const override { return sourceSamplePosition_ < samples_->size(); }
  void killNote() override { stopNote(); }

private:
  std::shared_ptr<SampleData> samples_;
};

std::unique_ptr<TrackState>
FileInstrument::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const {
  auto voice = std::make_unique<FileInstrumentVoice>(reduceForPositionalGroup(channel_config), position, detune, start_phase, samples_, send_a, send_b);
  voice->playNote(frequency, velocity, note_value);
  return voice;
}
