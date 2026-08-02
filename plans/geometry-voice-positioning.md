# Geometry-derived voice positioning: floor reflection, drum-kit layout, pitched-instrument spread

## Context

Today a voice's position is a single, static `SphericalPosition{ azimuth,
elevation, distance }` copied unchanged from the `InstrumentTrack` (or a
live `setAzimuth()` edit) down to whichever leaf voice renders it -
`SoundFontVoice`/`FileInstrumentVoice`/`OscilatorVoice` (all `:
public InstrumentVoice`) each spatially encode that one fixed point via
`InstrumentVoice::encodePosition()` (`InstrumentVoice.h:121`), using the
shared `computeAmbisonicGains()` SH function (`AmbisonicEncoding.h:121`).
Every note on a track - kick or crash, C2 or C6 - renders from the exact
same point. This plan makes position a function of **which key is played,
the instrument's own physical layout, and geometry relative to the
floor**, for every instrument type where that makes sense, while keeping
the bus, the SH function, and the send architecture completely untouched.

This document is read-only planning output. No code is changed.

## Part 0 - remove `HarmonicSeries`

### Findings

`HarmonicSeries` (`HarmonicSeries.h`/`.cpp`) is a `Track` (`TrackType::EFFECT`,
element name `"harmonicSeries"`) that fans a note out into a stack of
harmonic/undertone-detuned children. Every reference:

- `CMakeLists.txt:74` - `HarmonicSeries.cpp` in the `synth_engine` source list.
- `Song.cpp:10` (`#include "HarmonicSeries.h"`) and `Song.cpp:89`
  (`createTrack()`'s dispatch: `else if (name == "harmonicSeries") return
  make_unique<HarmonicSeries>();`) - the XML factory entry.
- `songs/padtest4.xml:8-10` - **a real, non-test song actually uses it**:
  ```xml
  <harmonicSeries undertone="0">
    <oscilator type="sine" level="0.5"/>
  </harmonicSeries>
  ```
  This is the one dependency that isn't just code. Per this project's own
  convention (never fold the user's own `songs/*.xml` edits into an
  unrelated commit), fixing this file is a **separate, user-reviewed step**,
  not something this removal silently absorbs. The natural replacement is
  the bare `<oscilator type="sine" level="0.5"/>` (drop the wrapper - a
  single sine child with `undertone="0"` and default `voices_=256`/`from_=1`
  reduces to something far short of the full harmonic stack anyway, so
  there's no faithful one-line equivalent; flag for the maintainer to
  choose what this patch was actually going for).
- `plans/arpeggiator.md:47` - lists `HarmonicSeries` alongside other
  instrument types in prose; historical planning doc, not touched (old
  plans aren't edited after the fact in this repo).
- No test file references it (`tests/` grep is clean) and no
  registry/enumeration outside `Song.cpp`'s single `createTrack()` dispatch
  - `InstrumentProvider`/`InstrumentSet` never construct one (it's reached
    only via user-authored XML, like `NoteMultiplier`/`Oscilator`, not via
    the SoundFont/GM registration path).
- Nothing in `docs/` mentions it by name (checked `docs/commands.txt`,
  `docs/known_bugs.md`, `todo.txt`).

### Removal steps

1. Delete `HarmonicSeries.h`, `HarmonicSeries.cpp`.
2. `CMakeLists.txt:74` - remove the source-list entry.
3. `Song.cpp:10` - remove the `#include`; `Song.cpp:89` - remove the
   `createTrack()` branch (an unrecognized-element song will now hit the
   existing `assert(0)` fallback at `Song.cpp:100-101`, same as any other
   unknown tag - correct, not a regression, since post-removal
   `"harmonicSeries"` genuinely is unknown).
4. Flag `songs/padtest4.xml` to the maintainer for a manual fix (see
   above) - do not edit it as part of this same commit.
5. Build + full test suite green with no `HarmonicSeries` symbol left
   anywhere (`grep -ri harmonicseries` repo-wide returns nothing outside
   git history).

This is a clean, self-contained first step precisely because so little
depends on it - which is also why it's worth doing first: it keeps the
instrument-type audit below honest (three real instrument types -
SoundFont, FileInstrument, Oscilator/Noise/LFO as generic-instrument
leaves - not four).

## Findings: the instrument-type hierarchy and the encode/distance path

### Class hierarchy

- `Track` (`Track.h`) - the universal tree node. Declares the shared
  `playNote(config, position, frequency, detune, velocity, start_phase,
  note_value, sends)` contract (`Track.h:51`) with a default
  implementation that just fans out to `getChildren()` - the pattern every
  pure wrapper (`NoteMultiplier`, `EnvelopeFilter`, `ResonantFilter`, ...)
  relies on without its own override.
- `Instrument : public Track` (`Instrument.h`) - adds `harmonic_`/
  `subharmonic_` (loaded/stored, `Instrument.h:14-25`) and a `prepare()`
  hook. Leaves: `Oscilator`, `Noise`, `LFO`, `FileInstrument`,
  `GenericInstrument`. `SoundFontInstrument` (`SoundFont.cpp:1464`, no
  header of its own - defined and used only inside `SoundFont.cpp`) is
  *also* `: public Instrument` but is never directly XML-constructible
  (see below).
- `InstrumentSet` (`InstrumentSet.h`) - factory interface (`createInstrument`/
  `createAll`); `SoundFont` (`SoundFont.h`/`.cpp`) is the only
  implementation, wrapping a parsed `.sf2` (`SoundFontFile`, `tsf_preset`
  entries with `bank`/`preset`/`regions`, `SoundFont.cpp:113-171`).
- `InstrumentProvider` (`InstrumentProvider.h`) owns one `shared_ptr<Instrument>`
  per registered name (`instruments_by_name`), built once at startup from
  `loadSoundFont()` (GM aliases + every native preset name,
  `InstrumentProvider.h:23-77`) plus one built-in `Oscilator` ("Electric
  Piano", the ctor). **This is the crucial fact for scoping Part 2/3**:
  `createTrack()`'s XML dispatch (`Song.cpp:74-103`) has no case for
  `"soundFontInstrument"` at all - the *only* way a song's `<instruments>`
  list ever reaches an SF2 preset is through `GenericInstrument`
  (`getElementName() == "genericInstrument"`), whose `prepare(provider)`
  (`GenericInstrument.h:33-35`) resolves `concrete_instrument_ =
  provider.getInstrumentByName(getName())` - a `shared_ptr` **shared
  across every track/song that references that same name**. A raw
  `SoundFontInstrument` is therefore never itself a serializable,
  per-song, per-track-customizable node; `GenericInstrument` is the only
  node that is, for anything SF2-backed. `PercussionTrack : public
  InstrumentTrack` (`PercussionTrack.h`) is a thin `TrackType` marker for
  note-entry/UI purposes only - it does not change how `instrument_id_`
  resolves an instrument, so "is this track's instrument actually a GM
  drum kit" cannot be read off the track type; it has to come from the
  resolved instrument itself (its SF2 preset's `bank` field, see Part 2).

### Position storage and the encode/distance path today

- `SphericalPosition { azimuth, elevation, distance }` (`SphericalPosition.h`) -
  degrees/degrees/"arbitrary units" per its own doc comment. **This plan
  needs an absolute physical unit** (meters) to compute a reflection delay
  in seconds; see "Units" below for the resolution.
- `InstrumentTrack` (`InstrumentTrack.h:23-31`) stores one fixed
  `azimuth_/elevation_/distance_` (settable live via `setAzimuth()` etc.,
  `InstrumentTrackState.h:325`, and `Player.cpp:156` for the one currently
  wired live control) and hands it to `InstrumentTrackState`'s ctor
  (`InstrumentTrack.cpp:11`).
- `InstrumentTrackState::render()` (`InstrumentTrackState.h:18-75`) is
  **the actual per-note-on call site**: on every pending note-on event it
  calls `instrument->playNote(getChannelConfiguration(), position_,
  ev.getFrequency(), 1.0f, ev.getVelocity(), -getRandF(),
  ev.getNoteValue(), sends_)` (`:46`) - `position_` is the track's one
  static value, `ev.getNoteValue()` is already the resolved MIDI-key-like
  identity (see `TrackEvent`/`Note.h`) and is already threaded all the way
  down through every `playNote()` override that exists today. **No
  signature changes are needed anywhere to make position depend on the
  key** - `note_value` is already a parameter of the one call chain that
  matters.
- `SoundFontInstrument::playNote()` (`SoundFont.cpp:1470-1538`) recomputes
  `midiKey` from `frequency` (`:1481`, for SF2 region key-range matching -
  `region.lokey/hikey`), loops every matching region, and constructs one
  `SoundFontVoice` per match (`:1503`), passing the *same* `position` to
  every region. This is the exact seam Part 2/3 attach to (see below).
- `SoundFontVoice`'s own ctor (`SoundFont.cpp:900-906`) already adjusts the
  incoming position **once**, per-region, via `adjustPositionForPan()`
  (`:845-857`, folds the region's own SF2 `pan` generator into an azimuth
  delta, narrowed by `min(1, 1/distance)` - the exact "spread narrows with
  distance" law this plan generalizes) - direct, working precedent for
  "adjust position once at voice construction, bake it into the immutable
  base-class field, never recompute per block."
- `InstrumentVoice::encodePosition()` (`InstrumentVoice.h:121-142`) is
  where distance attenuation (`getDistanceGain()`, `:89`, plain `1/distance`)
  and the SH encode (`computeAmbisonicGains(getPosition())` feeding
  `AmbisonicVoiceEncoder::encodeBlock()`, `AmbisonicEncoding.h:227-256`,
  which interpolates gains linearly across the block from a persisted
  `prev_`) both happen, dry-path only, immediately before mixing - sends
  (`AuxA`/`AuxB`) are computed straight from `dry` with no distance
  attenuation at all (`:134-139`), confirming "sends are pre-distance" is
  already an invariant, not something this plan has to newly establish -
  it only has to not break it (the floor reflection tap must **not** touch
  `getSends()`/AuxA/AuxB at all).

### Seam summary (generic vs. SoundFont-only)

| Concern | Where it attaches | Instrument types |
|---|---|---|
| Extent (size) | New `SphericalPosition::extent` field, riding the existing position parameter through every `playNote()` | All (SoundFont, FileInstrument, Oscilator/Noise/LFO) |
| Floor reflection | New code in `InstrumentVoice` (base class - every leaf voice gets it), reading the voice's own already-resolved `getPosition()` | All |
| NoteMultiplier scatter | `NoteMultiplier::playNote()`, reading `input_position.extent` | All (its children can be any instrument type) |
| Percussion key table + per-hit jitter | **New code entirely inside `SoundFontInstrument::playNote()`** (`SoundFont.cpp:1470`) - nowhere else | SoundFont only |
| Pitched arc | Same function, same file, no other seam | SoundFont only |

`GenericInstrument` is **not touched** by Part 2/3 at all (beyond the
already-generic `getDefaultExtent()` forward covered under "Shared
design" below, which is pure delegation and doesn't know or care what
`concrete_instrument_` actually is). `SoundFontInstrument::playNote()`
already receives everything Part 2/3 needs - `position` (with `extent`),
`frequency` (from which it already derives `midiKey`, `SoundFont.cpp:1481`),
and direct access to `f->presets_[preset_]` (bank, and every region's
`lokey`/`hikey`) - so the entire mechanism is self-contained in one
function in one file. No `dynamic_cast`, no new `Instrument::isSoundFontBacked()`
virtual, no new XML element: the percussion layout is a hardcoded,
compiled-in table (the same style as `LaunchpadLayout.cpp`'s
`PERCUSSION_TABLE`), not something a song configures.

## Shared design

### Extent rides inside `SphericalPosition` - the load-bearing decision

`SphericalPosition` becomes:

```cpp
struct SphericalPosition {
  float azimuth = 0, elevation = 0, distance = 0;
  float extent = 0;  // physical half-width, meters. 0 = point source.
};
```

Because **every** `Track::playNote()` override in the tree - `Instrument`
leaves, `NoteMultiplier`, `GenericInstrument`, `SoundFontInstrument`,
every pass-through effect wrapper via `Track`'s own default
implementation - already takes `const SphericalPosition & position` as a
parameter, adding one field here makes extent available at every one of
those call sites **with no signature change anywhere in the tree**. This
is the single fact that keeps this plan's blast radius small: extent
doesn't need its own threading mechanism, a new `SendLevels`-style bundle,
or a parallel out-of-band lookup - it piggybacks on a struct that already
flows everywhere position flows, the same way `SendLevels` already
piggybacks `main`/`a`/`b` through the same calls.

Rendered angular half-width is always `atan(extent / distance)` -
computed at the point a concrete azimuth/elevation offset is needed
(floor-reflection geometry, percussion-table/arc conversion,
`NoteMultiplier` scatter), never stored as an angle. `extent <= 0` behaves
exactly like `distance <= 0` already does for direction (a well-defined,
harmless degenerate case - `atan(0/d) = 0`), so no new fallback branch is
needed in `computeAmbisonicGains()` itself.

**Shape ratio.** A horizontal:vertical ratio of 3:1 is not a new idea to
introduce - `NoteMultiplier.cpp:43` already does exactly this for its
own unison spread (`position.elevation += azimuth_offset / 3.0f;`). This
plan promotes that ratio to one named shared constant (proposed:
`constexpr float kExtentShapeRatio = 3.0f;` in `AmbisonicEncoding.h`,
next to `kAmbisonicOrder`/`cubeVertexDirections()` - the file already
holds the other shared spatial constants) and every consumer (percussion
table, arc, jitter, `NoteMultiplier`) divides its vertical component by
it instead of re-deriving its own ratio.

### Where the default extent comes from: `Track::getDefaultExtent()`

`InstrumentTrack` gains an explicit `extent_` field (default sentinel
`-1.0f`, meaning "not authored - use the instrument's own family
default"), stored/loaded exactly like `azimuth_`/`elevation_`/`distance_`
(`InstrumentTrack.cpp:21-27` / `:38-41`). The **resolved** value (falling
back to the instrument's own default when the sentinel is set) is
computed where the instrument pointer and the track's position are
already both in hand: `InstrumentTrackState::render()` (`InstrumentTrackState.h:46`),
immediately before the existing `instrument->playNote(...)` call -

```cpp
SphericalPosition resolved = position_;
if (resolved.extent < 0.0f) resolved.extent = instrument->getDefaultExtent();
```

`getDefaultExtent()` is a new virtual on `Track` (not `Instrument` - see
why below), default implementation delegating to the first child exactly
like `getChildChannelConfiguration()` already does (`Track.h:28`):

```cpp
virtual float getDefaultExtent() const {
  return getChildren().empty() ? 0.0f : getChildren()[0]->getDefaultExtent();
}
```

This one default covers every wrapper type for free - `NoteMultiplier`,
`EnvelopeFilter`, `ResonantFilter`, `BiquadFilter`, `Chorus`, `Distortion`,
`Amplifier`, `Compressor`, `Tremolo`, `Group` - exactly the case
`songs/padtest2.xml:30`'s `<multiply ... name="Piano group"><genericInstrument
name="Piano"/></multiply>` needs: the `<multiply>` node is what
`instrument_id_` actually points at there, and it must transparently
report whatever its child (`genericInstrument`) reports. Two real
overrides are needed:

- `GenericInstrument::getDefaultExtent()` - forwards to
  `concrete_instrument_->getDefaultExtent()` (`concrete_instrument_` is a
  separately-held pointer, not a tree child, so the base delegation
  doesn't already reach it).
- `SoundFontInstrument::getDefaultExtent()` - real logic, from the
  resolved preset's `bank`/`preset` (GM program number), see the default
  table below.

Every true leaf with no children and no override (`Oscilator`, `Noise`,
`LFO`, `FileInstrument`) falls through to the base case and returns
`0.0f` - exactly the "likely 0 unless the artist sets one" default the
brief asks for, with zero code written for those classes specifically.

### Perspective mirror

No new stored field, no new XML attribute: `mirror_sign` is computed
**fresh, inline, from `position.distance`** at the point `applyKeyOffset()`
runs (`SoundFontInstrument::playNote()`, see below) - `distance <= 1.0f ->
+1 (player)`, else `-1 (audience)`. There is no live per-note distance
edit path in this codebase today (the one live control is
`setAzimuth()`, `Player.cpp:156`; `InstrumentTrackState::setAzimuth()` is
the only live setter that exists), so evaluating this at note-on time
rather than "once, at song load" (as an earlier draft of this plan
proposed, with its own stored `InstrumentTrack` field) produces identical
results in every case this engine can currently produce, at the cost of
one branch per note-on instead of one at load time - a trivial trade
for not adding a field, a serialization path, or a migration concern
anywhere. If a live distance control is ever added later and the
load-time-only freeze genuinely matters then, this is a one-line change
to make (cache the sign once instead of recomputing it) - not a reason to
build the caching now.

### The offset-resolution algebra

Both the percussion table and the pitched arc reduce to the same final
step, computed inside `applyKeyOffset()`: `u`/`v` (normalized,
`[-1, 1]`, fractions of `position.extent`) -> physical offset (`x = u *
extent`, `y = v * extent / kExtentShapeRatio`) -> `delta_azimuth =
atan2(mirror_sign * x, distance)`, `delta_elevation = atan2(y, distance)`
(using `atan2` rather than `atan` for the same divide-by-zero robustness
reason as the floor-reflection geometry below - `distance` is never
negative, but can legitimately be very small for a track parked at
"player" distance, and `atan2(y, 0)` correctly saturates to +-90 degrees
instead of a NaN/inf).

### Where this lives in code

Everything - mode selection (none/percussion-table/pitched-arc), the
default table itself, and the resolution math - lives **inside
`SoundFontInstrument::playNote()`** (`SoundFont.cpp:1470-1538`), as one
small static helper it calls once per note-on, before its existing
per-region loop:

```cpp
static SphericalPosition applyKeyOffset(const SphericalPosition & position,
                                         int midiKey, const tsf_preset & preset);
```

- `preset.bank == 128` (the GM percussion-bank convention, already parsed
  into `tsf_preset::bank`, `SoundFont.cpp:115`) - look up `midiKey` in a
  hardcoded, compiled-in table (proposed below), same style as
  `LaunchpadLayout.cpp:191`'s `PERCUSSION_TABLE`, just keyed by GM note
  number directly instead of Launchpad pad position.
- Otherwise, `preset.bank == 0` and `preset.preset` (the GM program
  number) in a small curated set (0-7 = piano family, 46 = orchestral
  harp) - interpolate the pitched arc from `midiKey`'s position within
  the preset's actual mapped key range (min `lokey`/max `hikey` across
  `preset.regions` - already-parsed data, no new SF2 parsing).
- Anything else - return `position` unchanged. This is the entire "a
  non-SF2 instrument does nothing" contract, except it's not really
  about instrument *type* at all any more: a `FileInstrument`/`Oscilator`
  voice never reaches this function in the first place (it lives in
  `SoundFont.cpp`, not anywhere generic), so there is no type check to
  get right or wrong - the function simply doesn't exist on any other
  code path.

**No new XML at all for this.** The layout is compiled-in, exactly like
the existing GM percussion note-name table (`Note.h:88`) and the existing
Launchpad GM drum-pad layout (`LaunchpadLayout.cpp:191`) already are -
"the key specific percussion locations will be hardcoded inside the
SoundFont, so no need to configure them from the XML." The one existing,
already-planned override an artist has is `extent` (see below) - setting
it to `0` on the track collapses every key's offset to a point
regardless of table contents, which already *is* the off switch/point-
source escape hatch Part 3 asks for, with no separate mode flag needed.

`GenericInstrument::playNote()` (`GenericInstrument.h:13-29`) is **not
touched**: it already just forwards `position` (now carrying `extent`)
straight through to `concrete_instrument_->playNote(...)`, and when that
resolves to a `SoundFontInstrument`, `applyKeyOffset()` runs inside
*that* call, invisibly to `GenericInstrument`. Nothing outside
`SoundFont.cpp` needs to know an SF2 preset is involved at all.

### Per-feature scale factors

All multiply the resolved `extent`, never a standalone angle:

| Feature | Multiplier default | Rationale |
|---|---|---|
| Percussion key table | 1.0 | "The layout *is* the instrument's width" - by construction, table entries are normalized to exactly fill `[-1,1]`. |
| Pitched arc | 1.0 | Same reasoning - the arc's whole point is to span the instrument. |
| Per-hit jitter | 0.05 (~+-3 degrees at the "wraps around" close distance, see worked numbers below) | A small per-hit nudge, not a repositioning. |
| `NoteMultiplier` scatter | 0.3 (within the brief's 0.2-0.4 band) | A unison "chorus halo," not the whole instrument's width. |

### `NoteMultiplier`: what changes, and the zero-extent trap

**Today** (`NoteMultiplier.cpp:30-46`): `unisons_ >= 2` spreads voices at
fixed `azimuth_offset` steps from `-spread_/2` to `+spread_/2` **degrees**,
elevation coupled at `/3` - independent of distance, independent of any
notion of instrument size.

**After this plan**: `spread_`'s *unit* changes from degrees to a
dimensionless multiplier on `input_position.extent` (default ~0.3, table
above); the loop keeps its existing structure but computes
`half_width_deg = atan2(spread_ * input_position.extent, input_position.distance)
* 180/pi` in place of the old fixed `spread_/2`, and derives elevation
from `kExtentShapeRatio` instead of the current hardcoded `/3.0f`
(numerically identical default, now named and shared rather than a local
magic number).

**The zero-extent trap, and why extent-on-`SphericalPosition` avoids it.**
A naive reading of "reinterpret spread as a multiplier on extent" breaks
immediately for the common case of a plain `Oscilator`-based unison pad -
its own class-level default extent is (correctly) 0, and `multiplier *
0 = 0` regardless of the multiplier, silently killing the width entirely
for exactly the patches this feature exists for
(`songs/padtest1.xml:16,24`, `songs/padtest2.xml:16,24,30`, etc. all wrap
a plain `<oscilator>` in `<multiply ... spread="180">`). This is avoided
architecturally, not by giving `NoteMultiplier` its own separate fallback
constant: extent is resolved once, generically, at
`InstrumentTrackState::render()` (see above) *before* `playNote()` ever
reaches `NoteMultiplier`, so `input_position.extent` a `NoteMultiplier`
instance sees is never "the leaf oscillator's own hardcoded class
default" - it's the resolved value for *this track* (instrument-family
default, or the artist's own explicit `InstrumentTrack::extent_`). A
pad/bass track that wants its old wide unison wash back after this
change simply needs a nonzero `extent` set on the track (or on a future
per-`GenericInstrument`/`Oscilator` default this plan doesn't propose
adding, since "solo instrument extent ~0" is the documented family
default) - which leads directly into:

**Breaking change, explicitly accepted per the green-field brief -
decided, not left open.** `grep -rn spread= songs/*.xml` finds real,
non-test usage: `spread="180"` in `padtest1.xml`/`padtest2.xml`/
`padtest3.xml`/`padtest4.xml` (all wrap a plain `Oscilator`, distance is
set to `1` on those tracks - `padtest1.xml:70-73`), `spread="90"` in
`disco_test.xml`/`songtest11.xml`/`songtest14.xml`/`songtest15.xml`,
`spread="60"` in the one test fixture (`tests/fixtures/
ambisonic_envelopefilter_notemultiplier.xml:14`), `spread="80"` in
`epiano_test1.xml`/`epiano_test1.v0.xml`. Every one of these numbers will
mean something different (and, per the trap above, default to
**silent** width) after this change, unless the track also gets an
explicit `extent`.

**This plan adopts the explicit-`extent`-everywhere resolution**: a
track's spread is *always* a product of its own resolved extent, with no
hidden fallback anywhere in the chain. Requiring the artist to add
`extent="..."` to any track that wants `NoteMultiplier` width - rather
than having `NoteMultiplier` silently invent a floor value (`max(extent,
0.3)`) whenever it finds a point-source child - was considered and
rejected: a hidden floor would mean `extent="0"` on a track no longer
reliably means "point source" the moment that track also has
`unisons_ > 1` somewhere beneath it, which undermines the one invariant
this whole plan is built on ("store size; derive angles at render time" -
zero should always mean zero). Explicit `extent` keeps that invariant
intact everywhere, at the cost of every song listed above needing a
one-time, manual `extent=` addition to its wide-unison tracks to sound
as before - an acceptable, up-front migration for a pilot, not a subtler
behavior change hiding inside `NoteMultiplier` itself. Flagging the
affected songs for the maintainer to fix directly (per this project's
"don't bundle the user's own song edits into my commits" convention) is
still the right way to land the actual file edits, just not left open as
a design question any more.

Also note the pure-geometry boundary: `atan2(spread_ * extent, distance)`
saturates toward but never reaches 90 degrees (180 degrees total) - the
old `spread="180"` values (a deliberately hemisphere-wrapping wash) map to
a large `extent * spread_` product (e.g. extent 1.5, multiplier ~7 gets
to `atan2(10.5, 1) = 84.6 deg` half-width, ~169 degrees total, close but
not identical) rather than an exact reproduction; there is no
multiplier value that reproduces a literal 180 degrees at any finite
extent/distance, which is physically correct (a real physical source
cannot truly wrap a full hemisphere) but is one more reason this is a
"re-tune, don't expect bit-identical" migration.

## Part 1 - floor reflection (all instrument types)

### Geometry

Given song-level ear height `hl` (meters), a voice's own resolved
`distance d` and `elevation el` (its *final* position, i.e. after any
Part 2/3 key-offset resolution - "a low-placed kick automatically gets a
steeper, stronger floor tap than a high crash" falls out for free because
this stage consumes whatever position the earlier stage already produced,
it does not special-case percussion at all):

```
hs = hl + d * sin(el)          // source height above the floor
dh = d * cos(el)               // horizontal distance
hs' = max(hs, 0)                // clamp - see "Below-floor correctness"
p = sqrt(dh^2 + (hs' + hl)^2)   // reflected path length
delay = max(0, (p - d) / c) * sample_rate   // see below - two clamps, not one
gain = min(1, d / p) * k_refl                // k_refl = reflection_coefficient
elevation_refl = -atan2(hs' + hl, dh)        // atan2, not atan - see below
azimuth_refl = azimuth (unchanged)
```

`c = 343 m/s` (speed of sound in air at ~20C - a fixed constant, not a
song parameter; nothing in this engine's scope varies with temperature/
altitude).

### Below-floor correctness: why the brief's single clamp isn't sufficient

The brief's recommended rule ("clamp `hs` to >= 0 in the reflection
geometry only... continuous because of the coincidence property") is
correct **at and near** `hs = 0`, but doesn't, by itself, prevent a
*second*, independent problem for sources placed **far** below the floor:
once `hs` is clamped to 0, the reflected path length `p` is computed from
the *clamped* image, which can come out **shorter than the real, unclamped
direct distance `d`** - producing a negative `delay` and a `d/p` ratio
greater than 1 (an amplification, not an attenuation).

Worked example, using the brief's own scenario (`hl = 1.7`, `d = 3`,
`el = -90` degrees, i.e. "1.3m below the floor"):

- `hs = 1.7 + 3*sin(-90) = 1.7 - 3 = -1.3` -> clamped `hs' = 0`.
- `dh = 3*cos(-90) = 0`.
- `p = sqrt(0^2 + (0 + 1.7)^2) = 1.7`.
- Direct distance is still `d = 3` (unclamped, per the brief - "leaving
  the direct path rendering wherever the artist placed it").
- `delay = (1.7 - 3) / 343 = -0.0038` s - **negative**. Not implementable
  as a forward-reading delay tap.
- `d / p = 3 / 1.7 = 1.76` - **greater than 1**, i.e. before the
  `reflection_coefficient` scale, the "reflection" would be louder than
  the direct sound, which a passive floor reflection can never physically
  be.

This is not an edge case unique to exactly `el = -90` - any placement with
`hs` sufficiently negative (deep below-floor) and small `dh` triggers it,
because clamping `hs` folds a genuinely distant below-floor source to a
*nearby* image, decoupling `p` from `d` in a way the "coincidence at
`hs=0`" argument doesn't cover once you're well past that boundary, not
merely infinitesimally past it.

**Resolution - two additional clamps, applied after the geometry above,
not instead of the `hs` clamp:**

1. `delay = max(0, (p - d) / c)` - if the folded geometry ever implies a
   reflection arriving *before* the direct sound, treat it as arriving
   coincident with it (delay 0) rather than computing a negative tap
   position. This only ever engages for below-floor placements; an
   above-floor source always has `p >= d` (reflecting strictly adds path
   length), so this clamp is a true no-op for the entire normal-use range
   and only exists for the below-floor tail.
2. `gain_ratio = min(1, d/p)` - caps the geometric term at unity so the
   reflection coefficient remains a true upper bound on the reflection's
   loudness regardless of how the below-floor fold plays out. Also a
   no-op above the floor (`d/p <= 1` always holds there).

Both clamps are cheap (`std::max`/`std::min`, already-computed values) and
both are strictly inert for the primary "source above the floor" use case
- they exist purely to keep the below-floor tail (explicitly *not* being
modeled for realism, per the brief - "sources are placed below the floor
for effect") from producing an invalid negative-time tap or an
amplifying "attenuation." Use `atan2(hs' + hl, dh)` rather than a plain
`atan((hs'+hl)/dh)` for `elevation_refl` for the same reason: `dh = 0`
happens exactly whenever `el = +-90` (a source directly overhead or
underfoot), and `atan2` returns the correct +-90-degree limit there
directly rather than a division by zero.

### Worked examples (above-floor, normal range)

`hl = 1.7`, `c = 343`, `k_refl = 0.4` (mid of the proposed 0.3-0.5 band):

**A - level with the listener, moderate distance** (`d=5, el=0`):
`hs=1.7, dh=5, p=sqrt(25+11.56)=6.047`. `delay=(6.047-5)/343=3.05ms`
(146 samples @ 48kHz). `elevation_refl=-atan2(3.4,5)=-34.2 deg`.
`gain=(5/6.047)*0.4=0.331` (before the ground-absorption lowpass).

**B - a kick placed slightly low** (`d=3, el=-10`, a plausible resolved
elevation after Part 2's small percussion offset): `hs=1.7-0.52=1.18,
dh=2.95, p=sqrt(8.73+8.28)=4.13`. `delay=(4.13-3)/343=3.28ms`
(157 samples). `elevation_refl=-atan2(2.88,2.95)=-44.3 deg`.
`gain=(3/4.13)*0.4=0.291`.

**C - a crash placed slightly high** (`d=6, el=+15`): `hs=1.7+1.55=3.25,
dh=5.80, p=sqrt(33.6+24.5)=7.62`. `delay=(7.62-6)/343=4.73ms`
(227 samples). `elevation_refl=-atan2(4.95,5.80)=-40.5 deg`.
`gain=(6/7.62)*0.4=0.315`.

B vs. C shows the effect is real (steeper reflection angle and slightly
higher gain for the lower-placed piece) but modest in magnitude -
consistent with Part 2's own "elevation is garnish, keep vertical offsets
small" constraint. Don't oversell this as a dramatic per-drum effect in
the verification listening test; it's a subtle depth cue, most audible as
a difference in the *comb-filter color* between kit pieces, not as an
obviously "lower" or "higher" reflection.

**D - `d -> 0`**: `hs -> hl`, `dh -> 0`, `p -> sqrt(0 + (2*hl)^2) = 2*hl`,
`delay -> 2*hl/c` (the buffer-sizing bound below), `gain -> (0/2hl)*k =
0` - a source at the listener's own position has a reflection at maximum
possible relative delay but zero gain (`d/p -> 0`), i.e. inaudible. No
special-case needed; falls out of the formula directly, though note
`d<=0` is still first checked by the existing `computeAmbisonicGains`
convention ("no position set") upstream of this - a track that never set
a distance never reaches this geometry at all (stays diffuse), same as
today.

### Buffer sizing

Max relative delay is `2*hl/c`, at `d -> 0`, shrinking monotonically as
`d` grows (confirmed by example D and the general shape of `p-d`). Sized
**once, at song load**, from the song's own `hl` (never resized after -
`hl` is a song-level parameter, not live-editable, matching every other
per-voice-lifetime-fixed value in this codebase).

| `hl` | max delay | samples @ 48kHz | buffer memory/voice (float, mono) |
|---|---|---|---|
| 1.7 m (default) | 9.91 ms | 476 (+ a few for interpolation margin, ~512) | ~2.05 KB |
| 50 m (proposed engineering clamp) | 291.5 ms | 13,992 (~14,000 with margin) | ~56 KB |

At full polyphony (this codebase has no fixed voice cap - `docs/known_bugs.md`/
`InfoLine`'s voice count is informational only, not a limit - but a
realistic worst case for this project's own songs is on the order of a
few hundred simultaneous voices under heavy `NoteMultiplier` unison
stacking, e.g. `padtest1.xml`'s `unisons="32"` patches): 256 voices at the
default `hl` is ~525 KB total; 256 voices at the clamped `hl=50` is
~14.3 MB. Both are trivial next to this process's existing SF2 sample-
data footprint; the clamp exists to put an honest ceiling on the number,
not because 14 MB is actually concerning.

### RT-safety - matching existing precedent, not inventing a new one

The brief asks for "no allocation... in the audio callback; per-voice
state preallocated with the voice pool." **This codebase has no voice
pool.** Voices are heap-allocated directly inside the real-time audio
thread on every note-on today: `Player.cpp`'s audio thread calls
`state_.render(...)` (`Player.cpp:231`) which flows straight into
`InstrumentTrackState::render()`'s `instrument->playNote(...)` ->
`make_unique<SoundFontVoice>(...)` (`SoundFont.cpp:1503`) - a heap
allocation, on the audio thread, per note-on, already. `SoundFontVoice`
already conditionally heap-allocates its own per-voice `chorus_engine_`
in exactly this same constructor when `chorus_send_ > 0`
(`SoundFont.cpp:958-960`), and `NoiseVoice` seeds its own per-voice
`NoiseStream` the same way (`Noise.cpp:33-36`). **This plan follows that
same established pattern** rather than introducing a preallocated-pool
mechanism that doesn't exist anywhere else in the codebase: the
floor-reflection delay line and its `Biquad` (ground-absorption lowpass)
are allocated once, in `InstrumentVoice`'s constructor, sized from the
song's `hl` (threaded down the same way `getChannelConfiguration()`'s
sample rate already is), and never reallocated for that voice's
lifetime. If a future pass wants to eliminate note-on-time heap
allocation project-wide, that is a separate, larger undertaking - out of
scope here, and not a regression this plan introduces (it would be
extending an existing pattern either way).

### Shared primitive: extracting `ChorusEngine`'s delay line

`ChorusEngine::ChannelState` (`dsp/ChorusEngine.h:49-53`) inlines a
circular buffer (`buffer`, `write_pos`) with linear-interpolated read
directly inside `processChannel()` (`dsp/ChorusEngine.cpp:38-73`: write at
`write_pos`, read at `write_pos - delay` with wraparound and `i0`/`i1`/
`frac` interpolation). This read/write pair is exactly what the floor tap
needs (write the dry sample once per sample, read back at a
block-to-block-interpolated delay), so it should be pulled out into a new
`dsp/FractionalDelayLine.h`:

```cpp
class FractionalDelayLine {
 public:
  explicit FractionalDelayLine(int bufferLength);
  void write(float sample);
  float read(float delaySamples) const;  // linear interpolation, wraps
 private:
  std::vector<float> buffer_;
  int write_pos_ = 0;
};
```

`ChorusEngine::ChannelState` holds one of these instead of its own
`buffer`/`write_pos` pair, and `processChannel()`/`processSilence()` call
`write()`/`read()` instead of the current inline indexing - a pure
refactor, no behavior change, one tested implementation instead of one
embedded copy. `InstrumentVoice` gets its own instance (mono, sized per
the table above) for the floor tap. Smoothing (delay length changing
block to block as distance changes) uses the exact same technique already
proven in `ChorusEngine::processChannel()` itself: the delay length fed to
`read()` is linearly interpolated **per-sample** within the block from the
previous block's resolved delay to the new one (mirroring how the LFO
there already produces a continuously-varying `delay` per sample, just
driven by distance-derived change instead of an LFO) - not a new
smoothing mechanism, the same one this codebase already ships and has
already verified doesn't zipper (`tests/ChorusEngineTests.cpp`). Gain
smoothing reuses `AmbisonicVoiceEncoder`'s existing `prev_`-to-`target`
per-sample lerp (`AmbisonicEncoding.h:229-250`) unchanged - the reflected
tap is encoded through a **second** `AmbisonicVoiceEncoder` instance (its
own direction, its own gain), so this needs no new code, just one more
member.

**FileInstrument/Oscilator voices currently allocate no delay line at
all.** Since the floor tap moves up to the shared `InstrumentVoice` base
class (not `SoundFontVoice`), `FileInstrumentVoice`/`OscilatorVoice` gain
it automatically, at the same per-voice construction cost as `SoundFontVoice`
- there is no per-instrument-type special-casing needed here, which is
the entire point of Part 1 being "all instrument types": the mechanism
lives once, at the base class both leaf-voice-render paths already share.

### Parameters (song-level, on `Song`, same deviation-only storage
convention as `Song`'s existing bus-slot/tempo fields - `Song.h:34-35`,
`:158-159`)

| Parameter | Default | Notes |
|---|---|---|
| `earHeight` (`hl`) | 1.7 m | Standing adult ear height - matches the brief's own worked numbers. |
| `floorReflectionEnabled` | true (on by default, zero-config) | Off switch. |
| `floorReflectionStrength` (`k_refl`) | 0.4 | Mid of the proposed 0.3-0.5 band. |
| `groundAbsorption` | 0.3 (arbitrary [0,1], mapped to a lowpass cutoff - e.g. `cutoff = 20000 * (1 - absorption)^2` giving ~9.8kHz at the default, fully open at 0, ~1kHz-ish at 1) | Gentle by default - "a cue, not a phaser." |

`hl` is clamped to `[0.1, 50]` m at load time - the lower bound guards
against a degenerate zero-height listener (division/atan2 stays well-
defined at `hl=0` actually, since `dh`/`hs` don't divide by `hl`
anywhere, but a literal 0 is not a physically meaningful ear height and
would make the reflection's own gain/delay independent of `hl` entirely
at `d=0`, so clamp it away rather than special-case it); the upper bound
is the "engineering limit, not a taste limit" the brief asks for - at
`hl=50` this is no longer a fusion cue at all, it's a ~300ms slapback/
canyon echo, which is a legitimate (if unusual) thing to want, so it's
documented rather than forbidden, per the brief.

## Part 2 - per-key position offsets, percussion (SoundFont only)

### Algebra

Each table entry is `(u, v, delta_d)`: `u, v` normalized to roughly
`[-1, 1]` (fractions of the kit's `extent`, horizontal and vertical
respectively), `delta_d` a literal small distance offset in meters
(almost always 0 - kit pieces are, by default, coplanar; nonzero only
where a specific piece is deliberately pulled forward/back, e.g. a kick
placed a touch closer). At render time (inside `SoundFontInstrument::
playNote()`'s `applyKeyOffset()`, see "Shared design" above):

```
x = u * base.extent                       // meters, lateral
y = v * base.extent / kExtentShapeRatio    // meters, vertical
azimuth_offset  = mirror_sign * atan2(x, base.distance) * 180/pi
elevation_offset = atan2(y, base.distance) * 180/pi
distance_out = base.distance + delta_d
```

This reproduces the brief's own worked numbers directly: at `extent=1.2`
(default kit half-width) and `d=1`, a table entry at `u=1.0` (hi-hat, full
lateral extent) gives `atan2(1.2, 1) = 50.2 deg` - "roughly +-50 degrees
at distance 1"; the same entry at `d=5` gives `atan2(1.2, 5) = 13.5 deg` -
"+-13 degrees at distance 5." Matches the brief's stated targets closely
without any per-distance tuning - the whole point of driving this from
`atan2(extent/d)` instead of a fixed-degree table.

### Default table

Populated from the existing GM percussion layout already in this codebase
- `LaunchpadLayout.cpp:191-234`'s `PERCUSSION_TABLE`/`percussionFamilyForPad()`
(built for the Launchpad grid, but its family groupings are exactly the
semantic groups a stereo image should reflect) and `Note.h:88`'s
`percussion_names[]` (GM values 27-82, all 56 sounds - every GM
percussion key gets a real entry, not just the core rock-kit ones; see
"One shared table, every key" below for why). Proposed `(u, v)` per key
(`delta_d` omitted below - 0 for every entry, no piece is pulled forward/
back of the kit's own plane by default) - values are a first-pass tuning
target, not acoustically final (same caveat as this project's other new
constants, e.g. `SILENCE_KILL_FLOOR_DB`):

| GM # | Name | u | v | Family |
|---|---|---|---|---|
| 35/36 | Bass Drum (both) | 0.0 | -0.4 | Core, centered, low |
| 38/40 | Snare (acoustic/electric) | 0.15 | -0.1 | Core, just off-center |
| 37 | Side Stick | 0.15 | -0.1 | With snare |
| 39 | Hand Clap | -0.15 | -0.1 | Mirrors snare to the other side |
| 42 | Closed Hi-Hat | 0.55 | 0.3 | Hi-hat, one side (mirrored by perspective flag) |
| 44 | Pedal Hi-Hat | 0.55 | 0.1 | Same side, lower (foot pedal) |
| 46 | Open Hi-Hat | 0.55 | 0.35 | Same side |
| 41/43 | Low/Low-Floor Tom | -0.5 | 0.0 | Toms sweep low-to-high across `u` |
| 45/47 | Low-Mid/Low Tom | -0.2 | 0.05 | |
| 48/50 | Hi-Mid/High Tom | 0.3 | 0.1 | |
| 49/57 | Crash 1/2 | -0.7 / 0.75 | 0.5 | Wide, high - one each side |
| 51/59 | Ride 1/2 | 0.65 | 0.4 | Ride side (with hats) |
| 52 | Chinese Cymbal | 0.85 | 0.5 | Widest, highest |
| 53 | Ride Bell | 0.65 | 0.35 | With ride |
| 55 | Splash Cymbal | -0.85 | 0.5 | Opposite the ride side |
| 54 | Tambourine | 0.1 | 0.2 | Hand perc, shaken high-ish |
| 56 | Cowbell | -0.6 | 0.1 | Hand perc, opposite the ride side |
| 58 | Vibraslap | -0.7 | -0.1 | Hand perc |
| 60 | Hi Bongo | 0.4 | 0.15 | Bongo pair, high one wide/up |
| 61 | Low Bongo | 0.2 | -0.05 | Bongo pair, low one narrower/down |
| 62 | Mute High Conga | -0.3 | 0.0 | Conga trio, sweeping like the toms |
| 63 | Open High Conga | -0.15 | 0.05 | |
| 64 | Low Conga | -0.45 | -0.15 | Lowest/biggest conga, most central-low |
| 65 | High Timbale | 0.55 | 0.2 | Timbale pair |
| 66 | Low Timbale | 0.4 | 0.0 | |
| 67 | High Agogô | 0.7 | 0.35 | Agogô pair, wide/high like a cowbell/cymbal |
| 68 | Low Agogô | 0.55 | 0.15 | |
| 69 | Cabasa | 0.0 | 0.1 | Handheld shaker, roughly central |
| 70 | Maracas | 0.15 | 0.1 | |
| 71 | Short Whistle | -0.6 | 0.3 | Whistle pair, wide and high |
| 72 | Long Whistle | -0.7 | 0.35 | |
| 73 | Short Guiro | -0.4 | -0.1 | Guiro pair |
| 74 | Long Guiro | -0.5 | -0.1 | |
| 75 | Claves | 0.0 | 0.0 | Simple, dead center |
| 76 | Hi Wood Block | 0.3 | 0.05 | Wood block pair |
| 77 | Low Wood Block | 0.15 | -0.05 | |
| 78 | Mute Cuica | -0.2 | -0.2 | Cuica pair, low-ish |
| 79 | Open Cuica | -0.3 | -0.2 | |
| 80 | Mute Triangle | 0.6 | 0.4 | Triangle pair, wide and high |
| 81 | Open Triangle | 0.65 | 0.42 | |
| 82 | Shaker | 0.05 | 0.15 | |
| 27 | High Q | 0.1 | 0.05 | Electronic FX - small, varied spread |
| 28 | Slap | -0.1 | 0.05 | |
| 29 | Scratch Push | 0.2 | -0.05 | |
| 30 | Scratch Pull | -0.2 | -0.05 | |
| 31 | Sticks | 0.05 | -0.1 | |
| 32 | Square Click | -0.05 | -0.1 | |
| 33 | Metr. Click | 0.0 | 0.15 | |
| 34 | Metr. Bell | 0.0 | 0.25 | |

### One shared table, every key

Every GM percussion key (27-82) gets a real `(u,v)` entry, not just the
"core rock kit" ones - per direct instruction, this is a pilot testing
what the ambisonic bus can do, and inventing a stereo convention for a
conga/bongo/agogô/wood-block ensemble is no worse a guess than the rock-
kit table itself already is (also first-pass, also not acoustically
final). The Latin/hand-percussion/electronic-FX entries above reuse the
**same** `(u,v)` space the rock kit uses (both range roughly
`[-0.85, 0.85] x [-0.4, 0.5]`) rather than being carved into a separate,
non-overlapping region - there is exactly one shared table, keyed only by
GM note number, with no notion of "which family is currently active" to
arbitrate between.

This means a single `GenericInstrument`/track that somehow played both a
rock kit's keys (e.g. 36/38/42) and a Latin ensemble's keys (e.g. 60-64)
in the same pattern would get two genuinely different "instruments"
overlapping in the same positions (a bass drum and a low conga can
resolve to nearly the same point). **This is accepted, not guarded
against**: per direct instruction, avoiding that mismatch is the artist's
own responsibility for now (don't layer a rock kit and a Latin
percussion set on the same track) - there is no per-preset table
selection, no bank-specific sub-tables, and no runtime check for "is this
key from a different family than the last one played on this track."
One table, one lookup, GM note number in, offset out, always - keeping
the mechanism exactly as small as Part 2's original one-function,
one-file design, just with every key populated instead of the ones
outside the core kit defaulting to `(0,0)`.

`u`-sign convention above is "player perspective" (`mirror_sign=+1`) -
hi-hat/ride side at positive `u`, crash/splash spread symmetric about 0.
Naming: per the brief, instrument-neutral (`player`/`audience`), not
`drummerSide`/etc. - "player" means azimuth offsets exactly as tabulated;
"audience" mirrors them.

### Per-hit jitter

+-2-3 degrees, applied as a small random `(du, dv)` (in the same
normalized units, scaled so the resulting angle is roughly +-2-3 degrees
at the *current* distance - i.e. `du = jitter_deg/atan_scale`, computed
from the current `extent`/`distance` rather than being a flat angle
itself, so it too narrows with distance like everything else here) added
to the resolved `(u,v)` before the `atan2` conversion. Seeding: **not**
`TrackState::getRandF()`/`rand()` (`TrackState.h:336-337`) - that shared
global sequence is already consumed by `NoteMultiplier`'s own phase
randomization and `SongState.h:92,94`'s velocity/delay randomization at
unpredictable, call-order-dependent points, so seeding jitter from it
would make jitter values depend on unrelated musical randomization
elsewhere in the render, breaking "same seed -> same offsets"
reproducibility. Instead, follow `bus/GranularCloud.cpp`'s own precedent
(`kDirectionScatterSeed`, a fixed compile-time constant, chosen exactly
*because* a shared bus effect has no per-instance `getRandF()` draw to
seed from and still needs reproducible renders,
`bus/GranularCloud.cpp:48-52`): seed a per-voice `dsp::NoiseGenerator`
(`dsp/NoiseGenerator.h`, already the fast xorshift32 PRNG this codebase
uses for exactly this "deterministic, not audio-critical-path-random"
need) from a fixed constant combined deterministically with `note_value`
and the track's own internal id (e.g. `seed = kPercussionJitterSeed ^
(note_value * 2654435761u) ^ track_internal_id`) - two hits of the same
key on the same track get *different* jitter (mixed further by a
per-hit counter, if literally identical repeated jitter across all
repeats of one key is judged undesirable) while a full re-render of the
same song from scratch reproduces bit-identical jitter every time.

## Part 3 - pitched-instrument spread, harp and piano (SoundFont only)

Same `applyKeyOffset()` function inside `SoundFontInstrument::playNote()`,
same file, a second branch instead of the percussion-table lookup: `u` is
computed from the note's position within the resolved instrument's actual
mapped key range (`region.lokey`/`hikey` across all regions - already
parsed, `SoundFont.cpp:283`'s generator table via `region.lokey`/`hikey`,
no new SF2 parsing needed) rather than looked up per key: `u = 2 * (key -
lokey)/(hikey - lokey) - 1`, `v = u * arc_tilt` (`arc_tilt` a small fixed
constant, default 0 - "flat" arc, no elevation sweep, since Part 2's own
"elevation is garnish" argument applies here too). No table to populate -
this is the "different population strategy" the brief calls for, same
mechanism otherwise, same "no XML surface" treatment as the percussion
table.

Default extents (auto-detected by GM program number as described under
"Shared design" -> "Where this lives in code"): concert harp (program 46)
~0.5 m, piano family (programs 0-7) ~1.5 m. Mirror flag applies the same
way as Part 2 (flips which end of the arc holds the low notes) - "which
end has the low notes is not a parameter, it follows from the shared
perspective mirror," per the brief, satisfied directly since `u`'s sign
is what the mirror negates, same code path as Part 2.

On by default per the zero-config principle - satisfied entirely by the
nonzero default extent for these two families; there is no separate
"arc enabled" flag to default to true, since the arc branch inside
`applyKeyOffset()` is itself the auto-detected default whenever the
family match succeeds. An explicit `extent="0"` override on the track is
the entire "point source" opt-out the brief asks artists be given - no
second, mode-specific switch is needed alongside it.

## CPU cost

Per voice, per sample, the floor tap adds: one `FractionalDelayLine::read()`
(4-5 flops - two array reads, one subtract, one multiply-add for the lerp),
one `Biquad<float>::apply()` sample (5 multiply-adds, direct-form biquad),
and one second `AmbisonicVoiceEncoder::encodeBlock()` pass - 16 multiply-
adds per sample at order 3 (identical cost to the dry path's own existing
encode, since it's the same encoder class). Total: roughly **2x** a
voice's existing per-sample positional-encode cost (was ~16 MACs/sample
for the dry encode alone; now ~16 + 16 + 5 + 5 = ~42), plus one
`computeAmbisonicGains()` call per block (not per sample - 16 sin/cos-based
multiplies, negligible against 64 samples' worth of per-sample work at
the default `RENDER_EFFECTSAMPLEBLOCK`). No profiling was run as part of
this planning pass (all figures above are op-counts, not measurements);
recommend measuring via the existing `--render` offline harness
(`OfflineRenderer.cpp`) on a heavy-polyphony fixture (e.g. a
`NoteMultiplier unisons="32"` pad) before/after, rather than trusting
the op-count estimate alone once implemented.

Worst case at high polyphony: this is a per-sample cost multiplied by
voice count and sample count, so it scales linearly with both - at, say,
256 simultaneous voices and 48kHz, the extra ~26 MACs/sample/voice is
about 320M extra MACs/sec, which is small relative to a modern CPU's
throughput but not exactly free either given this project's existing
per-voice cost (SF2 envelope/LFO/filter evaluation, `SoundFont.cpp`'s
`render()`) - again, measure rather than assume "small" is automatically
fine at this project's actual worst-case song.

## Test plan

- **HarmonicSeries removal**: build clean, `ctest` 100% green with the
  class fully deleted; a repo-wide `grep -ri harmonicseries` (outside git
  history) returns nothing.
- **Geometry unit tests** (new, e.g. `tests/FloorReflectionTests.cpp`):
  hand-computed delay/direction/gain at the three worked distances/
  elevations above (A/B/C), `d -> 0` (example D), a large `hl` (e.g. 50,
  confirming the buffer-sizing bound `2*hl/c` is what's actually
  allocated, not exceeded), `hs = 0` exactly (confirms the
  direct-and-reflected-path-length-equal coincidence property, zero
  discontinuity either side of it), and `hs < 0` deep below-floor
  (confirms both new clamps - `delay >= 0`, `d/p <= 1` - actually engage
  and produce a valid, non-negative tap rather than the raw negative
  value shown in the worked below-floor example).
- **Extent scaling tests**: the same stored `extent` yields
  `atan(extent/d)` at several `d` values (spot-check against the brief's
  own "+-50 deg at d=1, +-13 deg at d=5" for `extent=1.2`); `extent=0`
  collapses every consumer (percussion table, arc, jitter, `NoteMultiplier`)
  to a point (zero angular offset) regardless of any multiplier;
  `kExtentShapeRatio` (3:1) applies identically wherever elevation is
  derived from a horizontal component.
- **`NoteMultiplier` tests** (extend `tests/PatternBlockOpsTests.cpp` or a
  new fixture): sub-voice scatter derives from `extent * 0.3` (the new
  default multiplier) rather than a flat degree value; works when the
  wrapped child is each of `Oscilator`/`FileInstrument`/a `GenericInstrument`
  resolving to an SF2 preset; scatter width changes with distance
  (narrower at `d=5` than `d=1` for the same `extent`/multiplier).
- **Floor-reflection tests per instrument type**: one fixture song per
  leaf voice type (`SoundFontVoice`/`FileInstrumentVoice`/`OscilatorVoice`)
  confirming the reflection tap is present (non-silent, correctly delayed/
  gained) for all three - this is the test that actually proves Part 1 is
  generic and not accidentally SF2-only.
- **Buffer-sizing test**: construct a voice at the clamped `hl=50` and
  confirm no out-of-bounds write/read on the delay line (ASan build, per
  this project's existing `-DSYNTH_ENABLE_SANITIZERS=ON` convention).
- **Jitter determinism**: two full renders of the same song/seed produce
  bit-identical jitter offsets; two *different* note-value/track-id
  combinations produce different jitter (confirms the seed mixing
  actually decorrelates, not just "is deterministic").
- **Offset-resolution tests**: `applyKeyOffset()` (a free function or
  static method in `SoundFont.cpp`, testable directly given a synthetic
  `tsf_preset`) matches the proposed default table's `(u,v)` for a
  spot-check of keys (35, 42, 49) when `bank==128`; arc interpolation
  produces the expected `u` at the lowest/highest/midpoint mapped key of
  a synthetic 2-octave test preset when the program number matches the
  piano/harp family; a preset matching neither condition returns its
  input position unchanged. Separately: a `GenericInstrument` resolving
  to the built-in "Electric Piano" `Oscilator` (not SF2 at all) never
  calls `applyKeyOffset()` in the first place - confirmed by the fact
  that no such call exists on that code path, plus a render-level
  regression test that its output position is unaffected by `note_value`.
- **Mirror test**: constructing the same table/arc lookup at
  `distance <= 1.0` vs. `distance > 1.0` produces negated `u` (and
  therefore azimuth offset) - confirms the single inline `mirror_sign`
  computation inside `applyKeyOffset()` actually flips consistently
  across both the percussion-table and arc branches, from the same
  underlying algebra.
- **Encode-direction render test**: via the existing debug-capture/
  render-and-inspect tooling this project already has for spatial
  verification (`tests/RenderTests.cpp`'s `renderSongOffline()`-based
  fixtures, `HeatmapChart`/`DiracAnalyzer`'s existing marker/heatmap
  plumbing for the manual scope check below) - confirm a resolved
  percussion-table/arc/floor-reflection direction actually reaches the
  bus at the expected azimuth/elevation, not just that the math function
  returns the right number in isolation.
- **Send levels unchanged**: an explicit regression test asserting
  `AuxA`/`AuxB` sample values for a voice are bit-identical with the
  floor reflection on vs. off, and with a nonzero vs. zero extent -
  confirms "sends stay pre-distance" and "the reflection doesn't feed the
  send taps" both hold, not just by code inspection.

## Verification checklist

1. `cmake --build build -j` clean, no new `-Werror` warnings (this
   project's `-Wsign-conversion` etc. are already strict - the new
   geometry code's `int`/`float` conversions for sample counts need
   explicit casts throughout, following this codebase's existing
   `static_cast<int>(...)` convention seen throughout `SoundFont.cpp`/
   `ChorusEngine.cpp`).
2. `ctest --test-dir build --output-on-failure` 100% green, including all
   new tests above.
3. Listening tests (manual, via `--render` to WAV and/or live playback):
   - A GM drum pattern at track distance 1 vs. 5 - kit should audibly wrap
     around the listener at 1, collapse to a tight, still-clearly-stereo
     front image at 5, same mix otherwise.
   - A harp glissando (ascending run across the mapped key range) -
     audible left-to-right (or right-to-left, depending on the mirror
     flag) sweep across the arc.
   - Floor reflection on/off A/B: a sustained tone (audible comb-filter
     coloration, not a discrete echo), a percussion pattern (subtle
     depth/height impression, per the modest worked-example numbers
     above - don't expect a dramatic effect), and **explicitly on a
     plain `Oscilator` voice** (proves Part 1 isn't accidentally
     SF2-only in practice, not just in the code path).
   - A source swept continuously from above the floor to below it (e.g.
     an automated or hand-edited elevation ramp from +30 to -60 degrees
     at fixed distance) - confirm no click or audible discontinuity
     through `hs=0` and into the below-floor clamp region.
   - Scope check: `HeatmapChart`'s marker layer (`HeatmapChart.h:20-26`'s
     `Marker`/`setMarkers()`) showing the resolved kit layout, and the
     existing `DiracAnalyzer`-backed directional heatmap
     (`dsp/DiracAnalyzer.h`, `plans/dirac-heatmap-scope.md`) showing
     energy visibly hopping between drum positions as the pattern plays -
     both are existing tooling, not something this plan needs to build.

## Implementation phasing

Each phase is separable and independently mergeable; cut lines are marked
explicitly since a maintainer may reasonably want to stop after any of
them.

1. **HarmonicSeries removal** (Part 0). Fully self-contained; the only
   external dependency is the maintainer's own fix to
   `songs/padtest4.xml`, done separately. *Cut line: safe to ship alone,
   no behavior change to anything else.*
2. **Extent/position model + `NoteMultiplier` conversion.** Add
   `SphericalPosition::extent`, `Track::getDefaultExtent()` and its two
   real overrides (`GenericInstrument`, `SoundFontInstrument`),
   `InstrumentTrack::extent_` and its resolution in
   `InstrumentTrackState::render()`, `kExtentShapeRatio` in
   `AmbisonicEncoding.h`, and the `NoteMultiplier` spread-unit change (no
   fallback floor - see "the zero-extent trap" above; existing songs with
   `spread=` need a manual `extent=` addition, flagged for the maintainer
   to apply directly). Touches everything downstream, so it goes before
   Parts 1-3, but adds no new audible geometry itself beyond
   `NoteMultiplier`'s changed units. *Cut line: shippable alone; nothing
   later depends on anything except `extent` existing and flowing
   correctly.*
3. **Shared delay-line primitive + floor reflection.** Extract
   `dsp/FractionalDelayLine.h` from `ChorusEngine` (pure refactor, its own
   sub-step with `tests/ChorusEngineTests.cpp` re-run to confirm no
   behavior change before any new code uses the extracted class), then
   add the floor tap to `InstrumentVoice`, the song-level parameters, and
   the below-floor clamps. Depends only on phase 2 (needs `extent` to
   exist for consistency, though the floor tap's own geometry doesn't
   actually consume `extent` directly - it consumes resolved `position`,
   which by this point already carries whatever Parts 2/3 will later
   feed it). *Cut line: shippable alone; Parts 2/3 are not required for
   this to be complete and correct on its own for all instrument types.*
4. **SoundFont-only offset mechanism + percussion table.** `applyKeyOffset()`
   and its percussion-table branch, entirely inside `SoundFont.cpp` - no
   `GenericInstrument` changes, no new XML. Depends on phase 2 (extent)
   and benefits from phase 3 being in place first (so a placed kick's
   floor tap is audible immediately) but doesn't strictly require it.
   *Cut line: shippable alone; Part 3 is a separate, smaller addition on
   top.*
5. **Pitched arc.** Smallest remaining increment - reuses every mechanism
   phase 4 built, just a different population strategy and a different
   default-extent family match. *Cut line: final increment.*

## Open questions for the maintainer

1. **`songs/padtest4.xml`'s `<harmonicSeries undertone="0"><oscilator
   type="sine" level="0.5"/></harmonicSeries>`** - what should this
   actually become? A bare `<oscilator>` drops the harmonic-stack
   behavior entirely; there's no mechanical one-line replacement that
   preserves intent, since the feature is being removed specifically
   because it doesn't work. Needs the maintainer's own judgment on what
   this patch was going for.
2. **`groundAbsorption` -> cutoff-frequency mapping** proposed above
   (`20000 * (1-absorption)^2`) is a first-pass shape, not derived from
   any measured material-absorption data - worth tuning by ear once
   implemented rather than treating the formula as load-bearing.
3. **Buffer-sizing clamp default (`hl` max 50m)** - confirm 50m is the
   right engineering ceiling for this project's actual use cases (a
   50m-tall listener height is already far into "deliberate slapback
   effect" territory per the worked buffer-sizing table); a tighter
   clamp (e.g. 10-20m) would cut worst-case per-voice memory
   proportionally with no loss of any realistic musical use case.
