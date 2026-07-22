#include "FileInstrument.h"

#include "SampleData.h"
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
    auto base_gain = decibelsToGain(getGainDB());
    auto gain = base_gain * getDistanceGain();
    auto inChannels = samples_->numberOfChannels();

    // Common case: a mono sample file, positioned like every other leaf
    // voice - encode it via this voice's own position (InstrumentVoice::
    // encodePosition()), rather than broadcasting the same raw value into
    // every output channel (which used to be harmless back when the
    // output was forced to MONO, but would be spatially wrong now that
    // the destination is a genuine ambisonic width - W/Y/Z/X need
    // different relative gains, not identical values).
    if (inChannels == 1) {
      if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));
      for (int k = 0; k < frames; k++) {
	int i = getSourceSamplePosition();
	stepForward(1);
	dry_[static_cast<size_t>(k)] = i >= samples_->size() ? 0.0f : samples_->getChannelData(0)[i] * gain;
      }
      return encodePosition(dry_.data(), frames);
    }

    // A pre-baked multi-channel file already matching this voice's exact
    // ambisonic channel count (e.g. a pre-encoded B-format recording) -
    // there's no single mono source to spatially spread, so this copies
    // it directly instead of going through encodePosition() at all; the
    // file's own channels already ARE the spatial content. A file with
    // some other channel count (e.g. a plain 2-channel stereo sample) has
    // never been supported here (pre-existing, out of scope) and still
    // hits the assert below.
    auto outChannels = getChannelConfiguration().numberOfChannels();
    auto channels = regularChannelsFor(getChannelConfiguration());
    if (getSendA() > 0.0f) channels.push_back(Channel::SendA);
    if (getSendB() > 0.0f) channels.push_back(Channel::SendB);

    SampleData output(channels, frames);
    auto send_a = output.getChannel(Channel::SendA);
    auto send_b = output.getChannel(Channel::SendB);

    for (int k = 0; k < frames; k++) {
      int i = getSourceSamplePosition();
      stepForward(1);

      float raw0 = 0.0f;
      if (i >= samples_->size()) {
	for (int l = 0; l < outChannels; l++) {
	  output.getChannelData(l)[k] = 0.0f;
	}
      } else if (outChannels == inChannels) {
	for (int l = 0; l < outChannels; l++) {
	  output.getChannelData(l)[k] = samples_->getChannelData(l)[i] * gain;
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
  std::vector<float> dry_;
};

std::unique_ptr<TrackState>
FileInstrument::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const {
  auto voice = std::make_unique<FileInstrumentVoice>(channel_config, position, detune, start_phase, samples_, send_a, send_b);
  voice->playNote(frequency, velocity, note_value);
  return voice;
}
