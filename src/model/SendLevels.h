#ifndef _SENDLEVELS_H_
#define _SENDLEVELS_H_

// Bundles the 3 send levels Track::playNote()'s whole call chain threads
// down to each leaf voice, in one struct instead of a loose float parameter
// per send (which would otherwise mean growing playNote()'s already-long
// signature - and every one of its overrides/call sites - each time a new
// kind of send is added, as happened once already going from 1 to 2).
//
// main: how much of the voice's own sound reaches the regular (dry,
// positionally-encoded) ambisonic channels - InstrumentTrack::getSendMain(),
// 1.0 default (full signal, i.e. today's behavior unchanged). a/b: how much
// additionally reaches the shared send bus's two slots - InstrumentTrack::
// getSendA()/getSendB(), 0.0 default (see bus/BusEffect.h). All three are
// plain linear multipliers, read directly per sample in the audio callback
// (InstrumentVoice.h) - a human edits them in dB (a perceptual/log scale is
// far easier to dial a subtle send with than a linear fraction), but that
// conversion happens only at the control-surface/file-format boundary
// (InstrumentTrack::loadParameters()/storeParameters(),
// Controller::setTrackSendA()/setTrackSendB()/setTrackSendMain()) - never
// here, and never per sample. All three are applied the same way, in the
// same place, by the voice itself: see
// InstrumentVoice::encodePosition() for main/a/b, and SoundFontVoice's own
// per-voice chorus taps and FileInstrumentVoice's multi-channel path for
// the two spots that scale their own extra contribution to the regular
// channels by main independently (they add to those channels *after*
// encodePosition() already ran, so they'd otherwise escape it).
//
// A default-constructed SendLevels{} (main=1.0, a=b=0.0) is also the
// correct value to pass down to a modulator's own recursive playNote() call
// (see Oscillator.cpp, GenericInstrument.h, SoundFont.cpp) - a modulator's
// own rendered AudioBuffer is never itself spatially mixed into the audible
// output (only its raw phase content is read back out), so it must always
// render at its own full, un-attenuated level regardless of what the
// carrier voice's own sends are set to - inheriting the carrier's actual
// send_main there would incorrectly alter FM modulation depth whenever a
// user turns Send Main down for that track.
struct SendLevels {
  float main = 1.0f;
  float a = 0.0f;
  float b = 0.0f;
};

#endif
