#ifndef _AUDIOBUFFERUTILS_H_
#define _AUDIOBUFFERUTILS_H_

#include "AudioBuffer.h"

// Forces `data` to have a real (possibly silent) Main channel, preserving
// any AuxA/AuxB it already carries. renderChildren()/VoiceState::
// renderChildren() report zero Main channels whenever every child is
// currently inactive - correct for a plain passthrough node, but wrong
// for an effect with its own internal delay line or other persistent
// per-sample state that must keep advancing (and, for a delay line,
// keep being read from) even when its input has gone silent: skipping
// the whole block whenever `data.numberOfChannels() == 0` would freeze
// that state and silently drop whatever audio a delay line still has
// queued up (see effects/TapeDegradation.cpp/effects/Chorus.cpp's own
// use of this - both wrap an internal fractional-delay-based engine that
// needs a real buffer to keep writing/reading through, not just a signal
// to skip processing).
inline AudioBuffer ensureMainChannel(AudioBuffer data, int frames) {
  if (data.hasChannel(Channel::Main)) return data;

  AudioBuffer forced(1, data.hasChannel(Channel::AuxA), data.hasChannel(Channel::AuxB), frames);
  forced.zero();
  for (auto ch : { Channel::AuxA, Channel::AuxB }) {
    if (auto * src = data.getChannel(ch)) {
      auto dst = forced.getChannel(ch);
      for (int i = 0; i < frames; i++) dst[i] = src[i];
    }
  }
  return forced;
}

// Adds AuxA/AuxB (zeroed) to `data` where asked for and missing, keeping
// what it already carries - for an effect that keeps a send going through
// silence once it has carried one.
inline AudioBuffer ensureAuxChannels(AudioBuffer data, bool aux_a, bool aux_b, int frames) {
  bool add_a = aux_a && !data.hasChannel(Channel::AuxA), add_b = aux_b && !data.hasChannel(Channel::AuxB);
  if (!add_a && !add_b) return data;

  AudioBuffer forced(data.regularChannelCount(), data.hasChannel(Channel::AuxA) || add_a, data.hasChannel(Channel::AuxB) || add_b, frames);
  forced.zero();
  for (int c = 0; c < data.regularChannelCount(); c++) {
    auto src = data.getChannelData(c);
    auto dst = forced.getChannelData(c);
    for (int i = 0; i < frames; i++) dst[i] = src[i];
  }
  for (auto ch : {Channel::AuxA, Channel::AuxB}) {
    if (auto * src = data.getChannel(ch)) {
      auto dst = forced.getChannel(ch);
      for (int i = 0; i < frames; i++) dst[i] = src[i];
    }
  }
  return forced;
}

#endif
