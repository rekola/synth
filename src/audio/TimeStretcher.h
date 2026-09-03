#ifndef _TIMESTRETCHER_H_
#define _TIMESTRETCHER_H_

#include <vector>

// Pitch-preserving tempo change for a single mono signal, via SoundTouch
// (LGPL v2.1 - see THIRD_PARTY_LICENSES.md) - Resampler.h's cousin for a
// tempo mismatch rather than a sample-rate one. SampleTrackState::
// triggerClip() uses this instead of resampleMonoLinear() when a clip's
// own recorded tempo (SampleContent::getOriginalTempo()) disagrees with
// the song's current one, since a plain resample would also shift pitch -
// wrong for "the same recording, just fit to a different tempo."
// External-library-facing code, so it lives alongside SampleFileLoader.h
// here rather than in dsp/, whose building blocks stay dependency-free.
//
// `tempo_ratio` is SoundTouch's own tempo-factor convention: 1.0 = no
// change, less than 1.0 = slower/longer output, greater than 1.0 =
// faster/shorter output - exactly song_tempo / clip.getOriginalTempo()
// (see triggerClip()'s own call site), so the caller never has to invert
// or rescale it.
//
// `sample_rate` <= 0, `tempo_ratio` <= 0, an already-1.0 ratio, or an
// empty input all return `input` unchanged rather than running the
// algorithm for nothing (or dividing by zero) - the same defensive shape
// resampleMonoLinear() uses.
std::vector<float> stretchMono(const std::vector<float> & input, int sample_rate, double tempo_ratio);

#endif
