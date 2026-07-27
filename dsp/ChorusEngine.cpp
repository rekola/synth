#include "ChorusEngine.h"

#include <cmath>

using namespace std;

void
ChorusEngine::initChannel(ChannelState & ch, int bufLen, int voices, float channel_offset) {
  ch.buffer.assign(static_cast<size_t>(bufLen), 0.0f);
  ch.phase.resize(static_cast<size_t>(voices));
  for (int v = 0; v < voices; v++) {
    ch.phase[static_cast<size_t>(v)] = static_cast<float>(v) * (2.0f * float(M_PI) / static_cast<float>(voices)) + channel_offset;
  }
}

ChorusEngine::ChorusEngine(int channels, int sampleRate, int voices, float rateHz, float centerDelayMs, float depthMs, bool decorrelate)
  : sampleRate_(sampleRate), voices_(voices), rateHz_(rateHz), centerDelayMs_(centerDelayMs), depthMs_(depthMs) {
  int delaySamples = static_cast<int>(centerDelayMs_ * 0.001f * static_cast<float>(sampleRate));
  int depthSamples = static_cast<int>(depthMs_ * 0.001f * static_cast<float>(sampleRate)) + 1;
  int bufLen = delaySamples + depthSamples + 8;

  channels_.resize(static_cast<size_t>(channels));
  for (int c = 0; c < channels; c++) {
    // Only channels beyond the first get an extra fixed phase offset, and
    // only when decorrelate is set - see the class comment. For the
    // per-track (decorrelate = false) case this is 0 for every channel, so
    // all channels use identical voice phases (preserving whatever
    // position the input already has).
    float channel_offset = (decorrelate && c > 0) ? static_cast<float>(c) * (float(M_PI) / static_cast<float>(voices_)) : 0.0f;
    initChannel(channels_[static_cast<size_t>(c)], bufLen, voices_, channel_offset);
  }
  // AuxA/AuxB never decorrelate against each other or against Main - each
  // is its own independent lane, not a stereo pair being deliberately
  // diverged (see the class comment) - so both start at channel_offset 0,
  // same as Main channel 0.
  initChannel(aux_channels_[0], bufLen, voices_, 0.0f);
  initChannel(aux_channels_[1], bufLen, voices_, 0.0f);
}

void
ChorusEngine::processChannel(ChannelState & ch, float * buf, int frames, float delaySamples, float depthSamples, float dphi) {
  int bufLen = static_cast<int>(ch.buffer.size());

  for (int i = 0; i < frames; i++) {
    float x = buf[i];
    ch.buffer[static_cast<size_t>(ch.write_pos)] = x;

    float wet = 0.0f;
    for (int v = 0; v < voices_; v++) {
      float & phase = ch.phase[static_cast<size_t>(v)];
      float lfo = sinf(phase);
      float delay = delaySamples + depthSamples * lfo;

      float read_pos = static_cast<float>(ch.write_pos) - delay;
      if (read_pos < 0.0f) read_pos += static_cast<float>(bufLen);

      int i0 = static_cast<int>(read_pos);
      float frac = read_pos - static_cast<float>(i0);
      if (i0 >= bufLen) i0 -= bufLen;
      int i1 = i0 + 1;
      if (i1 >= bufLen) i1 -= bufLen;

      float s0 = ch.buffer[static_cast<size_t>(i0)];
      float s1 = ch.buffer[static_cast<size_t>(i1)];
      wet += (s0 + (s1 - s0) * frac) / static_cast<float>(voices_);

      phase += dphi;
      if (phase > 2.0f * float(M_PI)) phase -= 2.0f * float(M_PI);
    }

    buf[i] = (1.0f - mix_) * x + mix_ * wet;

    ch.write_pos++;
    if (ch.write_pos >= bufLen) ch.write_pos = 0;
  }
}

// Advances the delay line and LFO phases as if `frames` silent samples had
// been processed, with no buffer needed (nothing to write output into) -
// see this class's own process() doc comment for why.
void
ChorusEngine::processSilence(ChannelState & ch, int frames, float dphi) {
  int bufLen = static_cast<int>(ch.buffer.size());

  for (int i = 0; i < frames; i++) {
    ch.buffer[static_cast<size_t>(ch.write_pos)] = 0.0f;

    for (int v = 0; v < voices_; v++) {
      float & phase = ch.phase[static_cast<size_t>(v)];
      phase += dphi;
      if (phase > 2.0f * float(M_PI)) phase -= 2.0f * float(M_PI);
    }

    ch.write_pos++;
    if (ch.write_pos >= bufLen) ch.write_pos = 0;
  }
}

void
ChorusEngine::process(SampleData & data) {
  int mainChannels = data.regularChannelCount();
  int frames = data.numberOfFrames();

  float dphi = 2.0f * float(M_PI) * rateHz_ / static_cast<float>(sampleRate_);
  float delaySamples = centerDelayMs_ * 0.001f * static_cast<float>(sampleRate_);
  float depthSamples = depthMs_ * 0.001f * static_cast<float>(sampleRate_);

  int channelLimit = mainChannels < static_cast<int>(channels_.size()) ? mainChannels : static_cast<int>(channels_.size());
  if (mainChannels > 0) main_ever_present_ = true;

  for (int c = 0; c < static_cast<int>(channels_.size()); c++) {
    if (c < channelLimit) {
      processChannel(channels_[static_cast<size_t>(c)], data.getChannelData(c), frames, delaySamples, depthSamples, dphi);
    } else if (main_ever_present_) {
      processSilence(channels_[static_cast<size_t>(c)], frames, dphi);
    }
  }

  for (int a = 0; a < 2; a++) {
    auto * buf = data.getChannel(a == 0 ? Channel::AuxA : Channel::AuxB);
    if (buf) {
      aux_ever_present_[static_cast<size_t>(a)] = true;
      processChannel(aux_channels_[static_cast<size_t>(a)], buf, frames, delaySamples, depthSamples, dphi);
    } else if (aux_ever_present_[static_cast<size_t>(a)]) {
      processSilence(aux_channels_[static_cast<size_t>(a)], frames, dphi);
    }
  }
}
