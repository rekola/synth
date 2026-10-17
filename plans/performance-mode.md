# Performance mode

Custom (97) on a Launchpad is a grid of performance effects: columns are Live
View's tracks, rows are effects, a held pad applies its effect to its track,
and pad pressure deepens it. Nothing is written to the song. Lineage: hardware
trackers' performance mode (Polyend Tracker), with our own layout.

Stages, each tested on the hardware before the next starts.

## 1. The grid with existing machinery (built)

Roll, Duck, Throw A, Throw B and Orbit (`playback/PerformanceEffect.h`,
`SongState::setPerformance()`, `LeafTrackState::setPerformanceTargets()`,
`LaunchpadManager::handlePerformancePadEvent()`). Values are fixed in code,
not editable. User-facing description: `docs/launchpad.md`.

Roll re-fires every note that starts while it is held, from the note's own
start, until the note ends or the pad is released; notes already playing are
left alone. It needs notes, so sample tracks don't roll.

To try on the hardware: whether the fixed amounts feel right (Duck -12 dB,
Roll a sixteenth / 32nd / 64th, Orbit one to four turns a bar), and whether
Roll should also re-fire notes already playing when it is pressed.

## 2. Stutter

A buffer repeat of the track's own output: from the press, capture the
track's rendered (encoded) audio and loop a grid-synced slice of it while
held, with short crossfades at the seams; pressure shortens the slice
(quarter note toward a 32nd). A second mode picks slices at random, now and
then reversed, with longer crossfades - large grains, the sound broken into
pieces. Works on every track type, sample tracks included. The capture
buffer costs one channel per ambisonic channel only while held.

## 3. A low-pass filter in every voice, then its two rows

Only SF2 voices have a voice filter today. Give every `PositionedVoice` the
same `Biquad<double>` low-pass on its mono rows before encoding (one filter
per row: FM/Noise/PadSynth one, Oscillator one per bucket, Additive one per
row), instrument attributes `cutoff`/`resonance`/`cutoffKeyTrack`/
`cutoffVelocity`/`cutoffEnv` matching SF2's generators, no cost when unset.
Then `VoiceState::setFilterOffset()` (SF2 voices add it to their own cutoff)
and the Low-pass and Resonant low-pass rows.

## 4. Recording performance

Performance moves recorded during a take as automation, the way fader moves
are (`recordFaderAutomationIfArmed()`), which needs a pattern command per
effect.

## 5. Note mode's right column

The right-column buttons hold effects on the note track. The Launchpad X has
all eight free there; CC19 starts and stops note capture in Note mode today,
which has to move or be given up first.

## Open

- Several devices holding the same effect on the same track: the first
  release ends it.
- A song switch while a pad is held sends the release to the new song.
