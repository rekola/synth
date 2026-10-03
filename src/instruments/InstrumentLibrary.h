#ifndef _INSTRUMENTLIBRARY_H_
#define _INSTRUMENTLIBRARY_H_

#include "Tuning.h"

class InstrumentProvider;
class ChannelConfiguration;
class Track;

// Registers synth's own hand-built library instruments on top of whatever
// loadSoundFont() already registered: the GM synth-pad overrides
// (programs 89-96, each <envelope>+<padsynth>), the electric-piano FM
// override, the additive-piano fallback for piano.acoustic.grand, and the
// tape+padsynth mellotron. Every entry needs a reason to exist: it is a
// General MIDI program, or it is a real physical instrument. Called
// once at startup, after every loadSoundFont() call (Controller.cpp) - the
// GM pad overrides rely on this ordering (registerPath()'s last-write-wins
// rule) to take priority over the SF2 pads they replace, and the additive
// piano's own fallback role relies on it to see whether a SoundFont piano
// is already registered at piano.acoustic.grand.
//
// Plain permanent overwrites, not a revertible/toggleable layer - there is
// no way to get the SF2 pad back at runtime without reconstructing the
// InstrumentProvider (e.g. restarting synth without these registrations),
// a deliberate simplification.
void registerLibraryInstruments(InstrumentProvider & provider);

// Forces every registered library instrument's PadSynth node(s) to build
// their wavetable now, at a representative pitch, rather than leaving it
// to happen lazily inside the first real-time playNote() call - see
// PadSynth::prewarm()'s own doc comment for why (a real, non-trivial cost,
// measured ~10-16ms per octave region, that must never run on the audio
// thread). Call once, after registerLibraryInstruments(), as soon as the
// real ChannelConfiguration is known and before the audio thread starts -
// see Controller.cpp's own call site. Only warms `tuning`'s own octave
// region around middle C; a song played in a different tuning, or a note
// far from that register, still builds lazily on first use exactly as
// before - a real, accepted limitation, not a full fix for every
// possible first-note stall, just the overwhelmingly common preview case
// (auditioning a Library pad at a default pitch, default 31-EDO tuning).
void prewarmLibraryInstruments(InstrumentProvider & provider, const ChannelConfiguration & config, Tuning tuning);

// The single-instrument sibling of prewarmLibraryInstruments() above -
// walks one already-resolved Track's own subtree (not the whole provider
// taxonomy) for PadSynth nodes and forces each to build its wavetable at
// note_value's own octave region now. Exposed for callers that resolve a
// specific instrument themselves (a name, a taxonomy path, or a song's own
// instrument-pool slot) right before triggering a preview note at a
// specific pitch - the taxonomy-wide sweep above only ever warms one fixed
// middle-register octave, so any preview at a different octave, or of a
// song's own pool instrument (never covered by the taxonomy sweep at all,
// since it isn't provider-registered), still built its table lazily on the
// real-time audio thread without this - see OutlineView.cpp's own call
// site, on the UI thread, right before it pushes the PREVIEW_NOTE/
// PREVIEW_POOL_NOTE event.
void prewarmInstrumentTree(const Track & track, const ChannelConfiguration & config, Tuning tuning, int note_value);

#endif
