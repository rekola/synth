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

namespace {
// Own salt, not InstrumentVoice.h's kNotePhaseSalt - see SoundFont.cpp's
// identically-purposed kSf2StartDelaySalt for why a raw PCM recording
// needs a start-time delay (not a phase jitter) to decorrelate
// simultaneous unison copies, and why that's computed once frequency is
// known (playNote() below), not at construction.
constexpr uint64_t kFileStartDelaySalt = 0xC2B3A1D6F9174E08ull;
}

class FileInstrumentVoice : public InstrumentVoice {
public:
  FileInstrumentVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, float detune, std::shared_ptr<AudioBuffer> samples, const SendLevels & sends = {}, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false)
    : InstrumentVoice(channel_config, position, detune, sends, note_coord), samples_(samples), needs_decorrelation_(needs_decorrelation) {
    // Exact, click-safe start (sample 0) - the inherited kNotePhaseSalt
    // phase this constructor would otherwise leave in place is sized for
    // an oscillator's periodic phase, not a raw sample index; used as one
    // here it could seek up to a full second into the file at full,
    // unramped gain. See SoundFontVoice's own ctor for the identical fix.
    sourceSamplePosition_ = 0;
  }

  void playNote(float frequency, float velocity, int note_value) override {
    InstrumentVoice::playNote(frequency, velocity, note_value);
    if (needs_decorrelation_) {
      // Same period-relative, half-period-capped derivation as
      // SoundFontVoice::playNote() - see its own comment for the full
      // reasoning.
      constexpr float kStartDelayPeriodFraction = 0.5f;
      constexpr float kMaxStartDelaySeconds = 0.005f;
      float delay_unit = HashField(kFileStartDelaySalt).unit(note_hash_coord_, paramId("file_start_delay"));
      float delay_seconds = std::min(delay_unit * kStartDelayPeriodFraction / frequency, kMaxStartDelaySeconds);
      start_delay_samples_ = static_cast<int>(delay_seconds * getChannelConfiguration().getAudioOutSampleRate());
    }
  }

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

    // Leading start_delay_samples_ frames stay silent, without touching
    // sourceSamplePosition_ - see SoundFontVoice::render()'s identical
    // handling for why (the delay must hold real time still, not just
    // mute output).
    int k = 0;
    if (start_delay_samples_ > 0) {
      int delay_now = std::min(start_delay_samples_, frames);
      std::fill(dry_.begin(), dry_.begin() + delay_now, 0.0f);
      start_delay_samples_ -= delay_now;
      k = delay_now;
    }
    for (; k < frames; k++) {
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
  bool needs_decorrelation_ = false;
  int start_delay_samples_ = 0;
};

std::unique_ptr<VoiceState>
FileInstrument::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  auto voice = std::make_unique<FileInstrumentVoice>(channel_config, position, detune, samples_, sends, note_coord, needs_decorrelation);
  voice->playNote(frequency, velocity, note_value);
  return voice;
}
