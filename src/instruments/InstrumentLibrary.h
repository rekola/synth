#ifndef _INSTRUMENTLIBRARY_H_
#define _INSTRUMENTLIBRARY_H_

class InstrumentProvider;

// Registers synth's own hand-built library instruments on top of whatever
// loadSoundFont() already registered: the GM synth-pad overrides
// (programs 89-96, each <envelope>+<padsynth>), the additive-piano piano
// fallback/comparison instrument, and the tape+padsynth mellotron. Called
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

#endif
