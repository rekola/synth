#ifndef _SENDLEVELS_H_
#define _SENDLEVELS_H_

#include <cmath>

// Bundles the 3 send levels Track::playNote()'s whole call chain threads
// down to each leaf voice, in one struct instead of a loose float parameter
// per send (which would otherwise mean growing playNote()'s already-long
// signature - and every one of its overrides/call sites - each time a new
// kind of send is added, as happened once already going from 1 to 2).
//
// main: how much of the voice's own sound reaches the regular (dry,
// positionally-encoded) ambisonic channels - LeafTrack::getSendMain(),
// 1.0 default (full signal, i.e. today's behavior unchanged). a/b: how much
// additionally reaches the shared send bus's two slots - LeafTrack::
// getSendA()/getSendB(), 0.0 default (see bus/BusEffect.h). All three are
// plain linear multipliers, read directly per sample in the audio callback
// (InstrumentVoice.h) - a human edits them in dB (a perceptual/log scale is
// far easier to dial a subtle send with than a linear fraction), but that
// conversion happens only at the control-surface/file-format boundary
// (LeafTrack::loadParameters()/storeParameters(),
// Controller::setTrackSendA()/setTrackSendB()/setTrackSendMain()) - never
// here, and never per sample. All three are applied the same way, in the
// same place, by the voice itself: see
// InstrumentVoice::encodePosition() for main/a/b, and SoundFontVoice's own
// per-voice chorus taps - the spot that scales its own extra contribution
// to the regular channels by main independently (it adds to those
// channels *after* encodePosition() already ran, so it would otherwise
// escape it).
struct SendLevels {
  float main = 1.0f;
  float a = 0.0f;
  float b = 0.0f;
};

// Sends are persisted and edited in dB (a perceptual/log quantity is far
// easier to dial a subtle send with than a linear fraction), but the
// fields above are plain linear multipliers read per sample - these
// convert at the XML boundary (Track/LeafTrack's load/storeParameters()),
// with -100dB as "off", the same floor TreeNode's own conversion uses.
inline float sendDbToLinear(float db) { return db > -100.0f ? std::pow(10.0f, db * 0.05f) : 0.0f; }
inline float sendLinearToDb(float linear) { return linear <= 0.00001f ? -100.0f : 20.0f * std::log10(linear); }

#endif
