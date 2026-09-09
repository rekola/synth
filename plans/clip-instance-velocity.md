# Per-instance velocity for placed clips (note and sample)

## Context

A `Clip` (`src/model/Clip.h`) is shared content that can be placed at more
than one position - editing it through any one placement updates every
other placement immediately (Clip.h's own doc comment). That sharing is
exactly why a *placement-level* loudness knob is missing today: there is
no way to make one placement of a reused clip play softer or louder than
another without editing the clip's own shared content, which would change
every other placement too.

`ArrangementOps.cpp`'s `mixIntoSampleBackground()` already has a `gain`
parameter for exactly this reason, with an explicit comment that every
call site passes `1.0` because "a real per-instance loudness/velocity
concept doesn't exist yet." This plan adds that concept: a `velocity`
carried on the instance event itself (`Scene`'s row -> clip placement
map), not on the `Clip`, so it varies per placement the same way position/
loop/length already do at the clip level but this deliberately doesn't.

Scope: both clip kinds placed via `ArrangementOps::placeClipInstance()`
and resolved via `resolveInstanceAt()`/`resolveInstanceForBar()` - a note
clip (adjusts every note's own already-authored velocity, see "Combining"
below) and a `SampleTrack` clip (adjusts the voice's output gain, which has
no other velocity concept at all today - `SampleClipVoice`'s ctor comment
currently states outright that "there's no performance-velocity input for
a Session-view/transport-triggered clip the way a played note has"). Also
fully covers Session view's own live audition/queueing state
(`LaunchpadManager::triggered_pattern_by_track_`/`queued_pattern_by_track_`),
not just persisted arrangement instances - a queued swap carries its own
velocity through to whenever it actually launches, exactly like a
persisted instance's does.

Explicitly out of scope: a scene's own inline (non-clip) `Pattern` content
has no instance to attach this to and isn't touched; `SampleTrack`'s
always-on background bed (`Scene::getSampleBackgroundContent()`) is a
different concept from a real clip instance and stays at the neutral,
no-adjustment value.

## Design

**What velocity actually *is* in this codebase, checked rather than
assumed.** `InstrumentVoice.h`'s and `SoundFont.cpp`'s own note-on gain
computation:

```cpp
setGainDB(-gainToDecibels(1.0f / velocity));                               // InstrumentVoice.h:59
setGainDB(-voiceRegion_->attenuation - gainToDecibels(1.0f / velocity));   // SoundFont.cpp:1094
```

with `gainToDecibels(g) = 20*log10(g)` (`TreeNode.h:93`). Working through
that: `-gainToDecibels(1/velocity) == 20*log10(velocity)`, which
`decibelsToGain()` (`10^(db/20)`) converts straight back to `velocity`
itself. So normalized `velocity` (`Note::getVelocityAsFloat()`, `0..1`) is
used as a **plain linear amplitude gain** - not run through any
perceptual/power curve at this layer. The `dB` round-trip you see there is
only how it gets combined additively with the SF2 region's own
`attenuation` (also in dB) before converting back once - the same pattern
every other independent gain contribution in this codebase uses when it
needs to combine with another one.

**Consequence: combine two independent gains by multiplying them (adding
their dB), never by adding the raw values.** Two rejected approaches, and
why:

- A plain percentage-of-percentage multiply of the two *raw* `0..127`
  values (`note_velocity * instance_velocity / 127`) is actually the
  mathematically correct shape (two linear gains genuinely do compound
  multiplicatively), but it forces `127` (max trigger) to be the only
  possible neutral point, which makes it structurally impossible to ever
  play a clip *louder* than however it was recorded - multiplying two
  values that are each capped at `1.0` can only ever shrink or hold, never
  grow.
- A signed *linear* offset (`raw + trigger - 64`, floored at `0`) allows
  boosting, but isn't how gain actually composes: it's an absolute unit
  shift applied to values already on wildly different scales, so it swings
  a near-silent note (say, recorded velocity `1`) by a huge *relative*
  amount while barely moving an already-loud one by the same *absolute*
  amount - not how a real gain control (or a real velocity-sensitive
  instrument) behaves.

**The fix: the trigger velocity is a signed `dB` offset, not a linear
value at all - `±12 dB` across its full `0..127` range, `0 dB` at
`constants::DEFAULT_VELOCITY` (`0x40`/`64`, this codebase's own existing
"no real velocity given" default - see `constants.h`, already used
elsewhere for exactly this "nothing more specific to go on" role).** A
single small shared conversion, `ArrangementOps.h`:

```cpp
// A clip instance's own trigger velocity (raw 0..127, matching
// Note::velocity's own domain and constants::DEFAULT_VELOCITY's own
// "neutral" role) as a linear gain multiplier - a +-12dB curve centered
// on DEFAULT_VELOCITY, not a scale factor on the raw value: two
// independent gains compose by multiplying (equivalently, summing their
// dB), never by adding their raw values - see InstrumentVoice.h's/
// SoundFont.cpp's own velocity-derived setGainDB() calls, which already
// combine gain contributions exactly this way.
inline float instanceVelocityToGain(short raw_velocity) {
  auto db = (static_cast<float>(raw_velocity) - constants::DEFAULT_VELOCITY) * (12.0f / 64.0f);
  return db > -100.0f ? powf(10.0f, db * 0.05f) : 0.0f;
}
```

- **Note clips**: `combined = note.getVelocityAsFloat() *
  instanceVelocityToGain(active.velocity)`. At the neutral default
  (`instanceVelocityToGain(64) == 1.0`), *every* note is left completely
  unchanged, at every velocity - not just "medium stays medium," the
  actual bar this needs to clear for "must not affect any existing song."
  A quiet note now responds *proportionally* to the trigger, the musically
  correct way a gain control should behave, rather than either being stuck
  (the `min()` problem) or swinging by a disproportionate fixed amount
  (the raw-offset problem). Only the actual `!note.isOff()` sounding-note
  branch is touched - the separate `isAftertouch()` branch just above it
  in `SongState.h`'s row-scan is a pressure/modulation amount, not a
  loudness, and stays as-is.
- **Sample clips**: there's no independent "recorded velocity" to combine
  against - a `SampleClipVoice`'s nominal level is implicitly always
  "full" today (hardcoded `1.0f`) - so `instanceVelocityToGain()`'s result
  *is* the gain, directly. At `64` this is exactly `1.0` (today's
  unchanged behavior); `127` (max) pushes it to roughly `4x` (`+~12dB`,
  since `127` is `63` units above `64`, just shy of the full `+12dB`);
  `0` pulls it to roughly `1/4` (a full `-12dB`, `64` units below). A
  sample clip, like a note clip, can genuinely be played back louder or
  quieter than however it was recorded, not just attenuated down from a
  fixed ceiling.

No upper clamp is applied - any resulting boost is subject to whatever
downstream headroom/clipping protection the mixer already provides for
any other gain knob in this codebase, the same as `mixIntoSampleBackground()`'s
own pre-existing `gain` parameter. `±12 dB` (this plan's own chosen
constant, not derived from anything - a moderate, clearly audible range
without risking heavy clipping when an already-hot note gets the full
`+12dB` on top) is easy to retune later; nothing else in this design
depends on the specific number.

**A real tracker-style velocity column in `ArrangementGrid`
(`src/ui/tui/ArrangementGrid.h`/`.cpp`), not a special-purpose nudge
command.** Each track's bar cell gains a second subcolumn next to the
existing clip-index digit: a 2-hex-digit field mirroring `PatternEditor`'s
(`src/ui/tui/PatternEditor.h`/`.cpp`) own `ColumnType::VELOCITY` exactly -
`{:02x}` display, subcol 0/1 = high/low nibble, typed hex digits write
into whichever nibble the cursor is on and advance the same way
(subcol 0 -> 1 on the first digit; both filled advances to the next
column/track, mirroring `PatternEditor::offerInput()`'s own VELOCITY-
column handling). `ArrangementGrid`'s cursor gains a subcolumn dimension
(mirroring `PatternEditor`'s `col`/`subcol` split) so Left/Right (or Tab,
matching `PatternEditor`'s own Tab-cycles-subcol convention) can move onto
the velocity subcolumn before moving to the next track. A freshly placed
instance shows `40` (`0x40`/`64`, hex) here - the exact same
`constants::DEFAULT_VELOCITY` value already shown/used as a default
elsewhere in this codebase (e.g. the drum step-grid's own fixed audition
velocity), so it reads as a familiar "nothing special set" value rather
than a fresh magic number.

Shown only on an instance's own leading bar - the same bar the clip-index
hex digit already shows on (velocity is one property of the whole
placement, not per-bar data, so later bars stay blank there exactly the
way they already do for the clip-index digit, relying on the background
tint below for continuation). Editing on any bar within an active
instance's span (not just its leading one) still targets that one
placement's own underlying event, resolved the same way Backspace/Del
already locate it today (`active.start_row`) - writes back via
`Scene::setInstance()` with the resolved clip id unchanged, just the new
velocity. A no-op when the cursor's bar has no real instance active
(`Scene::kStopInstance`/`kNoInstance`), same gating `PatternEditor`'s own
VELOCITY column already has for an undefined note.

This fully replaces any separate nudge-command idea - direct nibble entry
is the tracker idiom, and it's how every other numeric column in this
codebase (velocity, delay, effect argument) is already edited.

**Where the number comes from at placement time.** Every current way an
instance gets placed is a *live performance capture*
(`Controller.cpp`'s note-capture/sample-capture paths, and Launchpad
Session view's Record-Arm-on assign) - there is no existing "pick a clip
index for this cell" command outside of recording one in, though the new
velocity column above now lets any already-placed instance's velocity be
retyped by hand afterward regardless of how it was placed, the same as any
other pattern data.

- **Launchpad Session view, assigning** (`LaunchpadManager::
  triggerSessionClip()`'s Record-Arm-on branch): the pad press that placed
  the clip already carries a MIDI velocity (`LaunchpadPadEvent::
  getVelocity()`, already the same raw `0..127` domain this field uses)
  that's read for note-entry (`handlePadEvent()`, gated on
  `LaunchpadProtocol::getModelInfo(...).velocity_sensitive`) but discarded
  entirely by `handleSessionPadEvent()` today. Threading it straight
  through as the new instance's velocity (same gate, falling back to
  `constants::DEFAULT_VELOCITY` - not `0x28`'s note-entry default - on
  non-velocity-sensitive hardware, so existing non-editing behavior is
  unchanged there) is "hit around the middle to place it as recorded,
  harder to boost it, softer to cut it" with no new UI at all.
- **Launchpad Session view, auditioning** (Record Arm off) and **queued
  swaps**: the same pad velocity becomes the triggered/queued pattern's
  own velocity (`LaunchpadManager::TriggeredPattern` and the
  `queued_pattern_by_track_` map both gain it, see Implementation sketch)
  and adjusts the immediate live trigger's loudness without writing
  anything to the song - lets a performer feel out a level before
  committing to it by holding Record Arm and pressing again, and a queued
  swap remembers the velocity of the press that queued it, not just its
  clip index.
- **`Controller.cpp`'s other `placeClipInstance()` call sites** (note-
  capture/sample-capture recording paths) keep passing the new
  parameter's default (`constants::DEFAULT_VELOCITY`, neutral) - nothing
  about those gestures maps onto a placement-level offset the way a
  Launchpad pad press does, and it's editable by hand afterward via the
  new column regardless.

**Visual feedback - a bipolar tint, not a one-directional dim.** A placed
instance's block in `ArrangementGrid` renders at full track-identity color
today regardless of anything, and must keep doing so at the neutral
default - unlike a one-directional "dim toward background" idea, which
would show *every* untouched legacy instance as partially dimmed. Instead,
reusing the same `instanceVelocityToGain()` conversion: `auto gain =
instanceVelocityToGain(active.velocity); auto alpha =
std::clamp(std::abs(gain - 1.0f), 0.0f, 1.0f);` then `bg =
bg.blend(alpha, kWhite)` when `gain >= 1.0f` (boosted - brightens), or
`bg = bg.blend(alpha, styles.window_bg_color)` when `gain < 1.0f` (cut -
dims toward the empty-cell look), applied before the existing selection/
playhead tints (`Color::blend()`, the same mechanism `tintForPlayhead()`/
every existing tint in this file and `PatternEditor` already uses).
Neutral renders pixel-identical to today; the direction of the tint
(bright vs. dim) tells you at a glance which way a placement deviates from
its own authored level, alongside the new column's exact hex readout.

## Implementation sketch

- **`ArrangementOps.h`**: new free function `instanceVelocityToGain(short)`
  as above - the one place this conversion is implemented; every other
  site below calls it rather than reimplementing the curve.
- **`Scene.h`**: `instances_by_track_id_` value type `string` ->
  `ClipInstance{string clip_id; short velocity =
  constants::DEFAULT_VELOCITY;}`. `setInstance()` gains a `velocity =
  constants::DEFAULT_VELOCITY` parameter; `getInstance()`/
  `getInstancesForTrack()`/`getInstancesByTrack()` return the new type.
  `clearInstance()` unaffected.
- **`ArrangementOps.cpp`**: `ActiveInstance` (`ArrangementOps.h`) gains
  `short velocity = constants::DEFAULT_VELOCITY`, filled in by
  `resolveInstanceAt()`/`resolveInstanceForBar()` from the resolved
  `ClipInstance` (irrelevant/left default for `kStopInstance`/
  `kNoInstance`). `placeClipInstance()` gains a trailing `short velocity =
  constants::DEFAULT_VELOCITY` parameter, forwarded to
  `scene.setInstance()` - every existing call site (`Controller.cpp`,
  tests) keeps compiling unchanged. `mixIntoSampleBackground()`'s callers
  pass `instanceVelocityToGain(active.velocity)` instead of the hardcoded
  `1.0f`.
- **`Song.cpp`**: `<instance row="..." velocity="...">clip_id</instance>` -
  reader parses an optional `velocity` attribute (default
  `constants::DEFAULT_VELOCITY`) alongside the existing row/text-content
  parse; writer emits it only when it isn't that default (omit-default,
  matching this codebase's general XML convention), and never for an
  `"OFF"` stop entry.
- **`SongState.h`**: at the note-scan site that already computes `float
  velocity = note.getVelocityAsFloat()` for a defined, sounding note (the
  `!note.isOff()` branch - not the separate `isAftertouch()` one just
  above it), replace it with `note.getVelocityAsFloat() *
  instanceVelocityToGain(active.velocity)` when `active.clip_index >= 0`
  (unchanged for the scene-inline-pattern branch, which never reaches
  this).
- **`RenderContext.h`**: `SampleTrackEvent` gains `float velocity = 1.0f`
  (already-combined gain, internal engine plumbing only - never
  persisted); `addPendingSampleStart()` gains a trailing `velocity = 1.0f`
  parameter, stored on the event. `SongState.h`'s clip-branch call site
  (the real per-instance one, not the always-on background-bed call a few
  lines above it) passes `instanceVelocityToGain(active.velocity)`.
- **`SampleTrack.cpp`**: `SampleClipVoice`'s ctor takes a `velocity`
  parameter (already-combined gain) and stores it into `velocity_`
  instead of hardcoding `1.0f` (update its own comment - it currently
  claims no such input exists). `SampleTrackState::triggerVoice()`/
  `triggerClip()` gain a trailing `float velocity = 1.0f` parameter
  threaded through to the voice; `render()`'s `SampleTrackEvent::START`
  handling passes `event.velocity` through to `triggerVoice()`.
- **`PlaybackControlEvent.h`/`Player.cpp`**: `PLAY_SAMPLE_CLIP`'s existing
  `parameter3` (unused today) carries the raw `0..127` trigger velocity,
  matching every other velocity value already threaded through this event
  type; the handler calls `instanceVelocityToGain(static_cast<short>(
  parameter3))` before passing the result to `triggerClip()`.
- **`LaunchpadManager.h`/`.cpp`**: `fireClipStep()`/`fireOrTriggerClipStep()`
  gain a trailing `short velocity = constants::DEFAULT_VELOCITY`
  parameter, staying in the raw `0..127` domain (each pushed `PLAY_NOTE`'s
  own raw velocity becomes `note.getVelocity() *
  instanceVelocityToGain(velocity)`, rounded; passed straight through,
  unconverted, as the new `PLAY_SAMPLE_CLIP` parameter for the sample
  path, since `Player.cpp`'s handler is what actually applies
  `instanceVelocityToGain()`). `TriggeredPattern` gains `short velocity =
  constants::DEFAULT_VELOCITY`; `queued_pattern_by_track_` becomes a
  small `{clip_index, velocity}` pair too, so a queued swap's own
  velocity survives until it actually launches. `triggerSessionClip()`
  gains a `short velocity` parameter, sourced in `handleSessionPadEvent()`
  from `ev.getVelocity()` the same `velocity_sensitive`-gated way
  `handlePadEvent()`'s note-entry path already does (default
  `constants::DEFAULT_VELOCITY`, not `0x28`, on non-velocity-sensitive
  hardware); forwarded into `placeClipInstance()`'s new parameter on the
  assign branch and into the immediate-launch `fireOrTriggerClipStep()`
  call on the audition branch.
- **`ArrangementGrid.h`/`.cpp`**: cursor state gains a subcolumn (clip
  digit vs. velocity nibble 0/1), mirroring `PatternEditor`'s `col`/
  `subcol`. Rendering: the leading bar of an active instance shows the
  velocity's `{:02x}` hex readout in its own new subcolumn, matching
  `PatternEditor`'s `ColumnType::VELOCITY` cell styling; `bg` gets the
  bipolar blend described above, only on the `active.clip_index >= 0`
  branch. Input: typed hex digits when the cursor is on the velocity
  subcolumn write into the resolved instance's own event (located via
  `active.start_row`, same as Backspace/Del already do) through
  `Scene::setInstance()`.

## Verification

1. `cmake --build build -j` clean, no new warnings.
2. `ArrangementOpsTests.cpp`: place an instance with a non-default
   velocity, confirm `resolveInstanceAt()`/`resolveInstanceForBar()` both
   return it unchanged; confirm the XML round-trip (`SongTests.cpp`) both
   preserves a non-default velocity and defaults a legacy `<instance>`
   with no `velocity` attribute at all to `constants::DEFAULT_VELOCITY`.
   A small direct unit test for `instanceVelocityToGain()` itself: exactly
   `1.0` at `64`, and symmetric-ish `~4x`/`~1/4x` at `127`/`0`.
3. `RenderTests.cpp`, covering the actual combining rule rather than just
   "quieter overall":
   - A clip triggered at `constants::DEFAULT_VELOCITY` renders identically
     to the same clip with no instance-velocity concept at all - the real
     bar for "must not change any existing song," checked at more than
     one note velocity, not just one.
   - A clip with a very-low-velocity note (e.g. `1`), triggered at
     `constants::DEFAULT_VELOCITY` vs. triggered at `127` (max), renders
     at a clearly different, *higher*, but still proportionate level in
     the second case - the specific failure both `min()` and the raw-
     offset approach had (either no change at all, or a disproportionate
     jump).
   - The same clip triggered at `127` renders measurably *louder* than at
     the neutral default, and at `0` renders measurably *quieter* -
     genuine bidirectional control, not just attenuation from a fixed
     ceiling.
4. A `LaunchpadManager`-facing test (extending the existing e2e harness,
   `tools/e2e/verify_launchpad_session.py`, or a `ControllerTests.cpp`
   unit test if one already drives Session view assignment) confirms a
   pad press's velocity ends up on the resulting placed instance, and
   that a queued swap's velocity survives until it actually launches.
5. `ctest --test-dir build --output-on-failure` still 100% pass.
6. Manual, on real Launchpad hardware: a hard pad press while Record-Arm
   assigning a clip visibly (brighter block, and a matching hex readout
   above `40`, in `ArrangementGrid`) and audibly plays back louder than a
   clip assigned with a soft press (dimmer block, hex readout below
   `40`); auditioning (Record Arm off) at different pad velocities changes
   the live preview's loudness without writing anything to the song
   (undo/reload confirms no state changed). Manual, in the terminal:
   retype an existing instance's velocity via the new column and confirm
   both the visual tint and the actual playback level change to match,
   including a very quiet recorded note becoming clearly, proportionately
   louder when the instance's own velocity is set well above `40`.
