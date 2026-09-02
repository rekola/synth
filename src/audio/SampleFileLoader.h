#ifndef _SAMPLEFILELOADER_H_
#define _SAMPLEFILELOADER_H_

#include <memory>
#include <string>

class AudioBuffer;

// A loaded file's mono buffer plus the sample rate it's actually at (the
// file's own, straight from its header - never resampled here, see
// loadMonoSample()'s own comment for why). `buffer` is nullptr, `rate` 0
// on any failure.
struct LoadedSample {
  std::shared_ptr<AudioBuffer> buffer;
  int rate = 0;
};

// Loads an audio file from disk into a single-channel (mono) buffer for a
// SampleTrack clip. A multi-channel file is genuinely downmixed (every
// channel averaged into one), not just truncated to channel 0 the way the
// old, removed FileInstrument::openFile() did.
//
// Deliberately does NOT resample to the project's current output rate -
// the returned buffer stays at the file's own native rate, and the
// caller (SampleContent::setNativeSampleRate()) is expected to remember
// that rate alongside it. Resampling to whatever rate is actually running
// happens later, on demand, at playback time (SampleTrackState::
// triggerClip()) - never here, and never in a way that discards the
// original: reopening the same song under a *different* output rate
// (e.g. a project recorded at 192kHz, later resaved under a 48kHz
// session) must never silently bake a lossy resample into what gets
// written back to disk, which is exactly what would happen if this
// function resampled eagerly and the caller only ever kept the resampled
// copy around.
LoadedSample loadMonoSample(const std::string & path);

// The write side - a SampleTrack clip's own sidecar .wav (Song.cpp's clip
// writer). `sample_rate` is the rate to tag the file with - always the
// buffer's own native rate (SampleContent::getNativeSampleRate()), never
// the current session's output rate, so a save can never silently
// downgrade a clip's stored audio just because the project happens to be
// open under a different rate right now.
bool writeMonoSample(const std::string & path, const AudioBuffer & buffer, int sample_rate);

#endif
