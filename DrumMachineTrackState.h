#ifndef _DRUMMACHINETRACKSTATE_H_
#define _DRUMMACHINETRACKSTATE_H_

#include "InstrumentTrackState.h"

// Runtime counterpart of DrumMachineTrack (see StatefulSongObject's
// model/state split - every song-model class mirrors into a *State
// object that owns live playback bookkeeping the model object itself must
// stay free of). Inherits InstrumentTrackState rather than bare
// TrackState because a drum machine's emitted notes need exactly the
// same retriggerVoices()/chokeExclusiveClasses()/voices_ machinery a
// pattern-driven InstrumentTrack already gets - see
// plans/drum-machine.md's Phase 4, which reuses that machinery verbatim
// rather than reimplementing choke/retrigger. Empty for now: no
// step-driven note emission is wired in yet (that's Phase 4's own
// getNotesForRow()-equivalent addition) - this class exists so
// DrumMachineTrack's createState()/createStateTree() state-tree
// machinery is already exercised before that logic lands.
class DrumMachineTrackState : public InstrumentTrackState {
public:
  using InstrumentTrackState::InstrumentTrackState;
};

#endif
