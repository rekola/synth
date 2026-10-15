# Performance mode: a low-pass in every voice, Custom's performance grid, live tuning

Three parts, built in this order. Each one is useful on its own. Part 2 needs
part 1's filter, and part 3 shares part 2's event path and Custom's right
column.

Lineage: a momentary, non-destructive per-track effect pad grid is the
performance mode of hardware trackers (Polyend Tracker). The filter follows
the SoundFont 2 voice filter, so every instrument matches what SF2 voices
already have. Live transposition and the just-intonation toggle are this
engine's own: neither a tracker nor a clip launcher has a tuning to change.

Everything in parts 2 and 3 is **performance state**. It lives on the audio
thread, it is never written to the `Song`, it is not journaled or undone and
it is not saved. Releasing a pad, or `performance-reset`, puts the sound back
exactly as the song has it. Recording performance moves as automation is a
later step (see Open questions).

## 1. A low-pass filter in every voice

Today only SF2 voices (`SoundFontVoice`, and `GenericInstrument`, which is
one) have a runtime voice filter: `lowpass_`, a `Biquad<double>` set from
InitialFilterFc/InitialFilterQ and moved per block by the mod LFO, the mod
envelope and channel pressure (`SoundFont.cpp`). Oscillator, Noise, FM,
PadSynth and Additive only shape their spectra statically (Additive's hammer
and pluck corners, the PadSynth/Oscillator `ExponentialLowpass` harmonic
filter). A `<resonantFilter>` effect track can wrap any of them, but that
filters after the ambisonic encode, so it runs once per channel (16 at order
3) for each voice. It is also a track, not an instrument parameter.

### Where it goes

All the leaf voices render one or more mono rows and then encode them
(`encodePosition()`, or `AmbisonicStackEncoder` for Oscillator buckets and
Additive rows). The filter goes on those rows before the encode, in
`PositionedVoice`, so it costs one filter per row:
- one row for FM, Noise and PadSynth;
- one per bucket for Oscillator;
- one per row for Additive.

Putting it in `PositionedVoice` also gives `SampleClipVoice` the filter,
which part 2 needs for sample tracks. The filter is the same `Biquad<double>`
low-pass that SF2 uses, with the same cents-to-Hz conversion and the same
Q-in-dB reading, so a given cutoff and resonance sound the same on every
instrument.

`PositionedVoice::filterRows(rows, stride, count, frames)` is called by each
voice just before it encodes. It is a no-op, costing nothing, while the voice
has no cutoff and no performance offset (see below).

### Parameters

The parameters are on `Instrument`, read and stored by its
`loadParameters()`/`storeParameters()`, so every instrument type gets them
and the track nodes (`model/TrackNodes.h`) carry them as attributes with no
other change:

| Attribute | Meaning | SF2 counterpart |
|---|---|---|
| `cutoff` | corner in Hz; absent or 0 = no filter | InitialFilterFc (8) |
| `resonance` | in dB, 0-96 | InitialFilterQ (9) |
| `cutoffKeyTrack` | 0-1: how far the corner follows the note's pitch, 1 = by the same interval | none (SF2 does it with a modulator) |
| `cutoffVelocity` | cents the corner opens at full velocity | the default velocity-to-cutoff modulator |
| `cutoffEnv` | cents the filter envelope moves the corner; the envelope uses `Envelope.h`'s ADSR, attributes `cutoffAttack`/`Decay`/`Sustain`/`Release` | ModEnvToFilterFc (11) |

The SF2 instruments keep their own filter and ignore these five. Their
cutoff comes from the font and from `GenericInstrument`'s generator
overrides.

There is no filter LFO. SF2's ModLfoToFilterFc stays SF2-only until an
instrument needs it.

### Tests

- `RenderTests` renders a saw through Oscillator with `cutoff` 500 Hz and
  checks that energy above 2 kHz falls by the expected amount against no
  filter.
- Resonance raises the level at the corner.
- An SF2 region and an Oscillator with the same cutoff and Q give the same
  magnitude at three frequencies, which checks that both use one filter.
- Key tracking doubles the corner an octave up.
- A voice with no `cutoff` renders bit-identical to today, which protects
  every existing song.

`docs/` gains the attributes (`docs/effects.md` or the instrument pages:
`fm.md`, `additive.md`, `padsynth.md`).

## 2. Custom: the performance grid

`GridMode::CUSTOM` (CC97) shows a blank grid today. It becomes the
performance grid:
- **Columns** are tracks: Live View's own filtered list
  (`live_.track_ids`), in the same order as Live View's columns, so the two
  grids never disagree about which column is which track.
- **Rows** are effects, one per row.
- **Holding a pad** applies that row's effect to that column's track.
  Releasing it ramps back. Several pads at once stack, across tracks and
  across effects.
- **Depth** comes from pad pressure on the Launchpad X and Pro MK3 (the
  press velocity, then the aftertouch while held). The Mini MK3 senses no
  pressure, so it gets each effect's fixed default depth.

Rows, top to bottom:

| Row | Effect | What holding does | Mechanism |
|---|---|---|---|
| 7 | Low-pass | closes the cutoff, up to 6 octaves with pressure | filter offset (part 1) |
| 6 | Resonant low-pass | closes it 2 octaves and raises resonance with pressure | filter offset |
| 5 | Repeat | retriggers every sounding note every 1/16 row, 1/32 with harder pressure | the `-Rxy` retrigger path (`scheduleNoteRetrigger()`) |
| 4 | Tune | plays new notes one EDO step up, a fifth up with pressure (the EDO's nearest step to 3:2) | a note-value offset at `noteOn()` |
| 3 | Duck | dips Send Main toward silence | `adjustSendMain()` on the voices, with a short ramp |
| 2 | Throw A | raises Send A to full (a reverb throw) | `adjustSendA()` |
| 1 | Throw B | raises Send B to full (a delay throw) | `adjustSendB()` |
| 0 | Orbit | turns the track around the listener, one turn per bar, faster with pressure | an azimuth offset, the `YLxx`/`YRxx` slide path |

Duck, the throws and Orbit act on an offset from the track's own value. On
release they glide back to whatever the song says now, even if the song
changed while the pad was held.

The rows are a fixed set for now. Making them assignable was the reason for
the name "Custom", and it comes later (see Open questions).

### Engine

One event, `SET_PERFORMANCE`, with:
- `parameter1` = track id;
- `parameter2` = effect (an enum `PerformanceEffect`);
- `parameter3` = depth 0-127, where 0 means release.

`TrackState` keeps a small `PerformanceOverlay` struct holding the depth of
each effect and the current filter offset, cents and resonance. Each block
it pushes changes to the active voices, and a new voice is started with the
overlay already applied:
- `VoiceState::setFilterOffset(cents, resonance_db)` is a new virtual
  alongside `adjustSendMain()`/`applyChannelPressure()`. `PositionedVoice`
  implements it for part 1's filter. `SoundFontVoice` implements it by
  adding to its `fres` sum.
- With no authored cutoff, a negative offset starts the filter from wide
  open (13500 cents, as SF2's open filter).

Every value is smoothed over about 10 ms, so no press clicks.
`performance-reset` (a command, also sent when a song is closed) zeroes
every overlay.

The Launchpad sends the event from `LaunchpadManager` on press, on
aftertouch and on release. A pad still held when Custom is left is released
first, so nothing can stay stuck on.

### LEDs

- Each row has one hue: two greens for the low-passes, white for Repeat,
  purple for Tune, red for Duck, the Send A and Send B colours for the
  throws, orange for Orbit.
- The pads are dim while idle and bright while held.
- A column with no track is dark.

### Note mode's right column

In `GridMode::NOTES`, CC19 starts and stops note capture. The seven buttons
above it are unused. They become the same effects, held on the track the
note grid plays:
- CC89 Low-pass, CC79 Resonant low-pass, CC69 Repeat, CC59 Tune, CC49 Duck,
  CC39 Throw A, CC29 Throw B.
- Buttons sense no pressure, so each gets its fixed default depth.
- Shift still turns all eight into the shift functions.
- Orbit has no button here; it is on Custom.

This adds no new note-repeat mode with eight rates: Repeat is one effect
among seven.

### Tests

- Unit tests for `PerformanceOverlay`: press, stack, release, and ramping
  back to a value the song changed meanwhile.
- `LaunchpadManager` tests against the fake backend: a pad press sends
  `SET_PERFORMANCE`, leaving Custom releases held pads, and Note mode's
  right column sends with the fixed depth.
- An e2e script, `verify_launchpad_custom_performance.py`, presses the
  Low-pass and Throw A pads and checks the LEDs.

## 3. Live tuning

Two changes, bar-quantized and performance state like the effects above:

- **Live transpose.** Every pitched note starts N EDO steps away from what
  the song says (`SongState`, added to `note_value` before `noteOn()`;
  percussion tracks untouched). It is set by `live-transpose-up`/`-down`
  (one step), `live-transpose-fifth-up`/`-down` (the EDO's fifth) and
  `live-transpose-reset`. Tuning corrections travel with their notes
  unchanged: a chord's just intervals stay just. Only its fit to the key's
  tonic shifts, by the EDO's own error for the interval moved. A
  just-scale live note looks up its correction by its original degree, so
  an otonal song transposed stays otonal.
- **Just intonation on/off.** `toggle-just-intonation` makes playback ignore
  every tuning correction, the notes' own and `scale_corrections`, so the
  song plays in plain EDO. Toggling back restores them. This is an A/B
  comparison: the notes and their corrections are not touched.

Both are queued with the next bar's changes, as `QUEUE_SCENE_CHANGE` is
(`SongState::pending_scene_`), so a change lands on the beat with the clips.
The info line shows the state while it is not the default: `Tr +3`, `JI off`.

On the Launchpad, plain presses of Custom's right column (shift still has its
own functions):
- CC89/CC79 transpose up/down one step;
- CC69/CC59 transpose up/down a fifth;
- CC49 resets the transpose;
- CC39 toggles just intonation;
- CC29 and CC19 are left free.

Changing the EDO itself live is not part of this. Notes are steps of one
EDO, and converting them is `set-song-tuning`'s job
(`plans/53edo-spelling-and-tuning-change.md`).

### Tests

- `SongState`: a transpose queued mid-bar changes notes from the next bar on,
  not before.
- A tuned chord transposed keeps its intervals to the cent.
- JI off renders a tuned note at its plain EDO frequency.
- Reset returns to the song's pitches.

## Docs

- `docs/launchpad.md`: the Custom grid and its right column, and Note mode's
  right column.
- `docs/user-interface.md`: the new commands.
- `docs/glossary.md`: "performance effect" and "live transpose".
- The CLAUDE.md Launchpad section: CUSTOM's meaning.

This supersedes `plans/launchpad-custom-mode.md`, an older brainstorm about
the removed percussion lanes, which this change deletes.

## Open questions

- Should the live transpose and JI state be saved with the song, or reset on
  load? Proposed: reset. They are performance state.
- How do performance moves get recorded? They could become `YMxy`-style
  automation during a take, as the faders' moves are
  (`recordFaderAutomationIfArmed()`), which needs a command per effect.
  Proposed: not in this change.
- The low-pass covers voices only. A low-pass on an effect track's or the
  master's output would need the post-encode filter (`ResonantFilter`) and
  costs per channel. Proposed: not in this change.
- Assignable rows: a per-song choice of which effect is on which row, saved
  with the song, which is when Custom would earn its name.
