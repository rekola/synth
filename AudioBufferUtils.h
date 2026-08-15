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

#endif
