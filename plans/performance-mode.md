# Performance mode

Custom (97) on a Launchpad is a grid of performance effects: columns are Live
View's tracks, rows are effects, a held pad applies its effect to its track,
and pad pressure deepens it. The right-hand button at the end of a row applies
that row's effect to the master - the whole mix. Lineage: hardware trackers'
performance mode (Polyend Tracker), with our own layout; the glitch effects
follow beat repeaters, DJ loop rolls, granular processors and trance gates.

Stages, each tested on the hardware before the next starts.

## 1. The grid with existing machinery (built)

Duck, Throw A, Throw B and Orbit (`playback/PerformanceEffect.h`,
`SongState::setPerformance()`, `LeafTrackState::setPerformanceTargets()`,
`LaunchpadManager::handlePerformancePadEvent()`). User-facing description:
`docs/launchpad.md`.

Roll (re-firing every note started while held) was built here and removed:
every other effect works on the sound, Roll on the notes, and the pattern's
`0Rxy` retrigger already covers it. Note-level effects (Pattern length,
Pattern play mode) are out of the performance grid for the same reason.

## 2. Stutter and Erode (built)

**Stutter** (`state/TrackStutter.h`): captures the track's rendered (encoded)
output from the next row (at once with the transport stopped) and loops a
grid-sized slice of it while held, with short crossfades at the seams;
pressure shortens the slice from a quarter note toward a 32nd. The capture
costs one channel per ambisonic channel, only while held. Works on every
track type.

**Erode** (`state/TrackErode.h`): a patchy grain engine on the track's live
output - grains of 40-250 ms from up to 300 ms back, random level, a few
cents of detune and a small turn about the vertical, silence between them;
pressure thins the grains out. A tape-machine version (wow, dropouts, hiss)
was tried and rejected.

To try on the hardware: the fixed amounts (Duck -12 dB, Orbit's speed,
Stutter's slices, Erode's grains and sparseness).

## 3. The default set and the master (built)

Built with the effects that exist (Stutter, Erode, Duck); the other rows are
dark until stage 4 and the Filter stage. Orbit and the throws are not done
on the master.

The rows are fixed in code for now (`performance::kGridRows`), as the
**default set**, top to bottom: Stutter, Shuffle, Erode, Shatter, Gate,
Crush, Filter, Duck. Orbit, Throw A and Throw B stay in the code but
leave the grid; a song-level `<performance>` element choosing the rows comes
later (below).

**The master.** The right column's eight buttons (unused in Custom today)
apply their row's effect to the whole mix, after the send bus returns are
added, so reverb and delay tails are part of it:

| Effect | On the master |
|---|---|
| Stutter, Shuffle, Grains, Gate, Crush, Filter, Duck | the same effect on the mix; Stutter and Shuffle can record the mix continuously there, so a grab is immediate from the last bar |
| Orbit | turns the whole sound field |
| Throw A/B | left out on the master: its Send A/B are the bus returns |

## 4. New effects

- **Shuffle**: the beat repeater's other half - the last bar, recorded
  continuously, played back as grid slices in a new order while held;
  pressure shortens the slices (down to a 64th).
- **Grains**: Erode's engine, generalised, with two presets:

  | Setting | `erode` | `shatter` |
  |---|---|---|
  | Grain length | 40-250 ms | 5-30 ms |
  | Look back | up to 300 ms | up to 1-2 s |
  | Pitch spread | ±8 cents | ±12 semitones |
  | Reversed grains | none | about half |
  | Spatial scatter | ±30° | the full circle |
  | Feedback | none | some |
  | Pressure | thins the grains | denser and wider |

- **Gate**: the track (or mix) chopped on and off on the row grid while
  held, the live sound continuing underneath; pressure speeds it from an
  eighth to a 32nd; ~2 ms edges.
- **Crush**: sample-rate reduction (holding each sample, the same for every
  channel, so the sound stays in place) and bit reduction (nonlinear per
  channel, smearing the image somewhat - acceptable for a harsh effect);
  pressure deepens both.

All of them keep the song clock running: while held the music underneath
carries on unheard, and on release you are back in time with it.

## 5. A filter in every voice, the expression axes, and the Filter row

On the master the Filter is one filter on the mix, the classic DJ filter -
it needs none of the voice work below and can come first.

Only SF2 voices have a voice filter today. Give every `PositionedVoice` the
same `Biquad<double>` low-pass on its mono rows before encoding (one filter
per row: FM/Noise/PadSynth one, Oscillator one per bucket, Additive one per
row), instrument attributes `cutoff`/`resonance`/`cutoffKeyTrack`/
`cutoffVelocity`/`cutoffEnv` matching SF2's generators, no cost when unset.

**Expression axes, per instrument.** An instrument says what each of a held
note's axes drives - the MPE dimensions:

| Axis | Source |
|---|---|
| Velocity (strike) | set once at the press |
| Pressure (press, Z) | continuous |
| Slide (Y) | continuous, controller 74 per note channel |
| Glide (X) | continuous, per-note pitch bend |

Each axis names one destination (cutoff, resonance, volume, Send A/B, ...)
with an amount, or none. Which destinations exist is decided later; what
matters now is that every axis has something to drive. A SoundFont's own
pressure modulators stay unless the setting overrides them; the per-effect
`aftertouch` flags (`resonantFilter`, `biquadFilter`, `tremolo`) become a way
of stating the same thing, or go.

The Launchpad X/Pro gives velocity and pressure only, and those are wired
first: note aftertouch from the note grid and MIDI, and the performance
grid's one Filter row. On the Filter pad the press closes the cutoff and the
instrument's velocity and pressure axes do the rest, so "velocity ->
resonance, pressure -> cutoff" is a two-axis filter on a Launchpad: the hit
sets one dimension, held for the press, and pressure moves the other. The
X's velocity is narrow (a hard hit lands around 65-92) and the Mini MK3 has
neither, so true two-axis control needs an MPE controller. MIDI input reads
neither pitch bend nor controllers yet (`AlsaMidiInput.cpp`), so slide and
glide wait for MPE input: one channel per note, controller 74 and
per-channel pitch bend.

Duck and Orbit keep their own meaning for pressure.

## 6. Recording performance

Performance moves recorded during a take as automation, the way fader moves
are (`recordFaderAutomationIfArmed()`), which needs a pattern command per
effect. With the song clock running, a move is simply "this effect from row A
to row B".

## 7. Time (on hold, decide after testing)

Not decided yet; revisit once stages 2-4 have been played with.

- **Tape stop.** True varispeed on the master (the engine renders r times as
  many song frames per output block and resamples; pitch, notes, envelopes
  and reverb slow together, r = 0 stops the song, r > 1 speeds it up), or
  per track a buffer slowing to a stop. Also: a bar-quantized tape stop of
  every track but one, the stopped ones re-entering in phase on the bar line
  after release (gesture open: hold the pad that stays, or toggle pads while
  the master button is held).
- **Holding time.** A stutter (or stop) that freezes the song clock instead
  of running over it: nothing is lost under the repeats and the song lands
  later by the hold. Coherent on the master; on a track it drifts unless it
  re-enters on a bar line. Recorded with wall-clock durations, like the
  `YMxy` glides, since song rows don't pass while time is held.
- Live audio input runs on the wall clock and would bypass varispeed; a
  sample take recorded across a stop would not line up.

## Later: the effect set in the song

The rows become the song's own `<performance>` element, one element per
effect, rows top to bottom, in the style of the track effects
(`<tapeDegradation preset="...">`): the element names the effect, `preset`
picks a named set of its settings, and any attribute overrides one.

```xml
<performance>
  <stutter/>
  <shuffle/>
  <grains preset="erode"/>
  <grains preset="shatter"/>
  <gate/>
  <crush/>
  <filter/>
  <duck/>
</performance>
```

Song-only: the effects are recorded into the song's automation, so the set
belongs with it. A song without one gets the default set. Like the rest of
the song it lives in the document, so editing it is journaled.

## Later candidates

Not in the default set, possible through the XML once built: Reverse, Half
speed; and a Stutter whose loop wears a little more on
every pass, a tape loop disintegrating as it plays.

## Open

- Several devices holding the same effect on the same track: the first
  release ends it.
- A song switch while a pad is held sends the release to the new song.
