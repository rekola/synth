#ifndef _NOTEORIGIN_H_
#define _NOTEORIGIN_H_

// Distinguishes InstrumentTrackState::noteOn()'s two call sites for the one
// subclass that cares - ArpeggiatorState (see its own comment). A plain
// InstrumentTrackState ignores this entirely; its retriggerVoices()-based
// note-on already behaves identically either way.
//
// LIVE: Player::handlePlaybackControlEvent()'s PLAY_NOTE case - a performer
// pressing keys/pads in real time, one at a time, at whatever moments they
// actually land. Building a chord up finger by finger shouldn't yank the
// arpeggiator back to step 0 on every added note.
//
// PATTERN: InstrumentTrackState::render(frames, instruments, context)'s own
// pending-events loop - a row of authored pattern data. Every such note is
// a fresh, deliberately-timed onset (the same convention retriggerVoices()
// already applies to a plain track's pattern-driven note-on - a repeated
// note there always retriggers, never sustains).
enum class NoteOrigin { LIVE, PATTERN };

#endif
