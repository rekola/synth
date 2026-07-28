# Make the Arpeggiator actually work (full chord, tempo-synced)

## Context

`Arpeggiator` (`Arpeggiator.h`/`.cpp`) already exists as a `Track` subclass
(`TrackType::EFFECT`), registered in `Song.cpp`'s factory (`name ==
"arpeggiator"`) and referenced in `todo.txt`'s old backlog notes, but it's a
non-functional stub: `playNote()`'s loop over children does nothing, and
none of its fields (`mode_`, `note_duration_`, `octaves_`, `gate_`) are
loaded from XML or used anywhere. No song currently uses `<arpeggiator>`.

Confirmed with the user:
- **Tempo-synced** — step timing follows the song's BPM (rows), not a free
  running ms/Hz rate, matching every other tracker's arpeggiator and the
  existing `int` field types (rows, not milliseconds).
- **Full chord support** — holding multiple notes on an arpeggiator track
  should arpeggiate across all of them, not just the single most-recent
  note. `octaves_` layers on top: the step sequence is built from every
  held note × every octave transposition (0..`octaves_`), pitch-sorted, not
  "each note's octaves before moving to the next note."
- **`mode_`** (`UP`/`DOWN`/`UP_DOWN`): step order through that combined
  pitch-sorted list — ascending wrapping to the bottom, descending wrapping
  to the top, or ping-pong (up then down, no repeated endpoint).
- **`gate_`**: how long (rows, same unit as `note_duration_`) each step's
  note actually sounds before being cut off, out of the step's full
  interval. `gate_ >= note_duration_` is effectively legato/no gap.

## Why this needs new state, not just filling in `playNote()`

Every existing note-generating `Track` (`NoteMultiplier.cpp` is the closest
working sibling) spawns all its children **once, synchronously, inside
`playNote()`, at note-on** — nothing re-triggers a child voice later, during
an already-running state's lifetime. Confirmed via `TrackState.h`/
`InstrumentTrackState.h`: `render(int frames)` (the per-block hook used for
voice trees) carries no tempo, and every existing call site treats each
note-on as fully independent (`InstrumentTrackState.h:57-61`: each note id
gets its own freshly-`playNote()`'d, independent voice tree in `voices_`).
Chord arpeggiation needs the opposite: **one persistent stepper per track**
that sees every note-on/off across the whole held chord and decides what to
play next — genuinely new territory, not a gap-fill.

`InstrumentTrackState::render(frames, instruments, context)` *does* have
tempo (`context.getBpm()`, `InstrumentTrackState.h:18`) right where
`instrument->playNote(...)` is currently called (`InstrumentTrackState.h:59`).
Routing tempo through there — rather than widening `Track::playNote()`'s
signature (which ~9 other subclasses override: `Oscilator`, `SoundFont`,
`FileInstrument`, `Noise`, `LFO`, `HarmonicSeries`, `GenericInstrument`,
`NoteMultiplier`, plus the `Track` base default) — keeps the change
contained to the one place that actually needs it.

## Design

**`Arpeggiator.h`/`.cpp`**: add getters (`getMode()`, `getOctaves()`,
`getNoteDuration()`, `getGate()`) and XML attribute loading in
`loadParameters()`/`storeParameters()`, mirroring `NoteMultiplier.cpp`'s
existing pattern exactly (`input.getInt(...)`/`output.set(...)`). Attributes:
`mode="up|down|updown"` (default `up`), `octaves` (default 0 = chord notes
only, no extra octaves), `noteDuration` (rows, default 1), `gate` (rows,
default 1 = legato). `playNote()` itself stays a plain pass-through (same
body as `Track::playNote()`'s default) — it's only reached as a fallback if
an `<arpeggiator>` is ever nested somewhere other than an instrument
track's root (out of scope below), so it should degrade gracefully rather
than silently doing nothing.

**New `ArpeggiatorState.h`/`.cpp`** (a `TrackState` subclass, own files like
`SoundFont.cpp`'s `SoundFontVoice`, not header-only like
`InstrumentTrackState.h` — the stepping logic is nontrivial enough to want
out-of-line implementation and its own focused test coverage):
- Constructed with the same shape `InstrumentTrackState` already threads
  through today (`ChannelConfiguration`, `SphericalPosition`, `SendLevels`),
  plus a reference to the `Arpeggiator` instance (for its config + wrapped
  child track(s), reached the same way `NoteMultiplier.cpp` iterates
  `getChildren()`).
- `held_notes_`: a small ordered vector of `{id, frequency, velocity,
  note_value}`, updated by `noteOn(id, freq, vel, note_value)` /
  `noteOff(id)` (called by `InstrumentTrackState` instead of the normal
  `stopVoices()`/`addVoice()` path — see below). Adding a note to an
  already-sounding chord does **not** reset the step position; the chord
  going from empty to non-empty **does** restart from step 0 (a fresh
  keypress restarts the pattern — standard arpeggiator behavior).
- `step_pool_`: rebuilt whenever `held_notes_` changes — the cross product
  of held notes × octave transpositions 0..`octaves_` (frequency doubled
  per octave — tuning-system-agnostic, correct for every EDO without
  needing `Tuning`/EDO-step math), sorted by final pitch ascending. `UP`/
  `DOWN`/`UP_DOWN` walk this single combined list, not each note's own
  octave run before the next note (per the user's confirmation above).
  `note_value` is passed through unchanged from the held note that
  generated each step (not EDO-aware-transposed) — a documented, low-risk
  limitation: only affects LED/UI "which note is playing" reporting for
  octave-shifted steps, never the actually-audible frequency.
- `render(int frames) override`: the per-block hook. Advances an internal
  sample counter (carrying remainder across blocks, not hard-resetting —
  avoids timing drift). `note_duration_`/`gate_` (rows) convert to samples
  via the already-existing `ChannelConfiguration::getSampleInterval(bpm)`
  (`ChannelConfiguration.h:40`), using whatever `bpm` was set on this state
  most recently (see below). When the step interval elapses (and the chord
  isn't empty): advance `step_index_` per `mode_`, call
  `child->playNote(...)` for the new step's note, and `addChild()` it under
  a **fresh incrementing id** (not a fixed slot) — so a previous step's
  release tail (if `gate_` cut it early, or on note-off) keeps rendering
  via the inherited `renderChildren()` mix alongside the new step, instead
  of being destroyed outright when a fixed-slot `addChild()` would
  overwrite it. When the gate interval elapses within a step, call
  `stopNote()` (not `killNote()`) on that step's own child, once. Prunes
  finished children each call, mirroring
  `InstrumentTrackState::clearFinishedVoices()`
  (`InstrumentTrackState.h:222-226`). `isActive()` is overridden: true
  whenever the chord is non-empty (keep getting rendered so stepping
  continues even mid-gate-gap with no child currently sounding) or any
  child is still active (release tails after the last note-off).
- `setBpm(float)`: a plain setter, called by `InstrumentTrackState` each
  block from `context.getBpm()` — no virtual signature changes needed
  anywhere else.

**`InstrumentTrackState.h` changes**: add a `unique_ptr<ArpeggiatorState>
arp_state_` member alongside `voices_`. In `render(frames, instruments,
context)`'s event loop (`InstrumentTrackState.h:32-71`), detect once whether
`instrument` is arpeggiator-rooted (`dynamic_cast<Arpeggiator*>(...)`) and,
if so, lazily construct `arp_state_` and route note-on/off/aftertouch events
to `arp_state_->noteOn()/noteOff()/applyAftertouch()` instead of the normal
`stopVoices()`/`instrument->playNote()`/`addVoice()` sequence; call
`arp_state_->setBpm(context.getBpm())` once per block. In the plain
`render(int frames)` override (`InstrumentTrackState.h:90-127`), mix
`arp_state_->render(frames)` into the accumulator alongside the (in this
case always-empty) `voices_` loop, so both overloads — whichever a caller
uses — stay correct.

## Files to change

- `Arpeggiator.h`/`.cpp` — getters, XML load/store, pass-through fallback
  `playNote()`.
- New `ArpeggiatorState.h`/`.cpp` — the stepper described above.
- `InstrumentTrackState.h` — detection + dispatch + `arp_state_` member in
  both `render()` overloads.
- A new example song (e.g. `songs/arptest1.xml`) with an
  `<arpeggiator mode="up" octaves="2" noteDuration="1" gate="1">` wrapping a
  simple `<oscilator/>`, playing a held chord (multiple simultaneous notes
  in one pattern row/column) to manually verify by ear.
- `tests/RenderTests.cpp` — a new test rendering a small fixture through
  `renderSongOffline()`/the existing `RecordingMixer` pattern (see
  `render_dirac_heatmap_peak_matches_encoded_azimuth_sweep` for the
  established shape of a "render, then assert something about the actual
  audio content" test in this file), asserting the arpeggiator's output
  actually cycles through multiple distinct pitches over the render window
  (e.g. via a simple pitch-detection or peak-frequency check per chunk)
  rather than holding one static tone — the concrete regression test for
  "does the stepping logic actually step."

## Explicitly out of scope (documented, not silently dropped)

- Arpeggiator nested anywhere other than an instrument track's root (e.g.
  under a filter) falls back to the plain pass-through `playNote()` body —
  no time-stepping, matching today's behavior for every other wrapper
  track. Extending detection to walk the tree is a follow-up if ever needed.
- EDO-aware `note_value` transposition for octave-shifted steps (frequency
  is correct for every tuning system via plain doubling; `note_value`
  reporting for those steps is approximate — see above).

## Verification

1. `cmake --build build -j` clean, no new warnings.
2. `ctest --test-dir build --output-on-failure` 100% pass, including the
   new arpeggiator render test.
3. `./build/musiceditor --render out.wav songs/arptest1.xml`, inspect the
   waveform/spectrogram (e.g. `ffmpeg`/`sox` or by ear) to confirm distinct
   stepped pitches at the expected tempo-derived rate, not one sustained
   tone.
4. Manually play `songs/arptest1.xml` in the UI: hold/release chord notes
   and confirm the arpeggio pattern updates live (adding a note extends the
   pattern without restarting it; releasing down to zero notes stops
   stepping and lets the last note's release tail finish).
