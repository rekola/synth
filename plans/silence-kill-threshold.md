# Envelope silence-kill threshold (SF2 + EnvelopeFilter)

## Context

SF2 release envelopes are often long (multi-second GM pad/string tails),
and custom instruments built on `<envelope>` (`EnvelopeFilterState`) can
have equally long releases wrapping arbitrarily expensive child chains
(e.g. `<multiply unisons="32">` around an oscillator). In both cases the
voice keeps consuming CPU for the entire release even once it's long
since inaudible. The existing retrigger cutoff (`plans/sf2-retrigger-
cutoff.md`) only reclaims a voice early when a *new* note-on arrives with
a matching identity or exclusive class - it does nothing for a voice
simply releasing naturally after its own note-off with no retrigger at
all (different pitch, or no new note ever played). This plan adds a
self-monitoring kill threshold: once a voice is in its release stage AND
its actual output gain has decayed below an audibility floor, free it
early via the exact same path that already frees a fully-decayed voice.

Voice budgeting/stealing (a polyphony cap) stays out of scope - this is
about recognizing a voice that's already effectively silent, not about
capping voice count.

## Step 1 findings - the envelope machinery, and its two consumers

**Representation: linear gain, not dB.** `EnvelopeState` (`EnvelopeState.h`)
- one instance per voice/effect for amplitude. `Segment` enum: `NONE,
DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE, DONE` (`:41`). `level`
(private, `:170`) is a linear amplitude multiplier, read via `getLevel()`
(`:164`); `process()` (`:153`) decays it multiplicatively
(`level *= powf(slope, numSamples)`) whenever `segmentIsExponential` is
set, which the amp envelope always does for `DECAY`/`RELEASE` - never
stored or read in dB. `TrackState::gainToDecibels()`/`decibelsToGain()`
(`TrackState.h:324-330`, standard `20*log10`/`10^(db/20)`) already exists
and is inherited by every consumer below.

**Release is not currently queryable** - the only public state query is
`isDone() const { return segment == DONE; }` (`:163`). No `isReleasing()`/
segment getter exists yet.

**How a voice already gets freed today (the reuse target).**
`nextSegment()` (`:62`) is called with the segment that just *finished*;
`nextSegment(RELEASE)` (or any value via the `default:` case, `:144`)
enters `DONE`, unconditionally zeroing `level`. This is exactly what
happens when a release's own timer counts down naturally - forcing it
early is indistinguishable from the release simply finishing sooner.
Whatever depends on `isDone()` for reaping keeps working unchanged.

**Consumer 1: `SoundFontVoice`** (`SoundFont.cpp`). `ampenv_`/`modenv_`
(`:1126`). `isActive()` (`:984`) is `... && !ampenv_.isDone()`, and
`InstrumentTrackState::clearFinishedVoices()` erases any voice whose
`isActive()` is false every block - so making `ampenv_.isDone()` true is
the entire "free this voice" mechanism, already relied on by natural
completion, `killNote()`, `stopNote()`, and the retrigger cutoff's
`fastRelease()`.

Gain is computed per 64-sample block (`constants::RENDER_EFFECTSAMPLEBLOCK`,
`constants.h:8`) in `render()` (`SoundFont.cpp:1267` on): `dryGainMono =
noteGain * ampenv_.getLevel()` (`:1323`), immediately before
`ampenv_.process(blockSamples)` (`:1326`). `noteGain` is
`decibelsToGain(getGainDB())` (static per-voice gain: region attenuation
+ velocity, baked in once at `playNote()`, `:979`) or, when a
`modLfoToVolume` tremolo is configured, that value modulated by the mod
LFO (`:1317`) - not itself part of the envelope, but already folded into
`dryGainMono`, so reusing that value directly is both simpler and more
accurate than recombining `getGainDB()` and `gainToDecibels(level)`
separately.

**Consumer 2: `EnvelopeFilterState`** (`effects/EnvelopeFilter.cpp`,
backs the `<envelope>` XML element wrapping custom instrument chains).
Uses the identical `EnvelopeState` class - `envelope_state_` (`:56`).
`applyEffect()` (`:16-39`) multiplies every sample by `gain =
envelope_state_.getLevel()` (`:26`) once per 64-sample block, advancing
via `envelope_state_.process(blockSamples)` (`:37`) right after. No
separate static gain term to combine here - `gain` *is* the entire
multiplicative factor being applied, so the check is simpler than SF2's.
`stopNote()` (`:45-48`) calls `envelope_state_.nextSegment(SUSTAIN)` -
the identical SUSTAIN->RELEASE transition SF2 uses.

`EnvelopeFilterState::isActive()` (`:41`) is overridden to depend *only*
on its own envelope (`!envelope_state_.isDone()`), not on
`TrackState::isActive()`'s default child-OR logic - and `stopNote()`'s
own comment is explicit: `// let children play`. A note-off never stops
the wrapped child voice(s) at all; only the envelope's gain ramps down
over them. The child (which could be an expensive chain, e.g. 32 unison
oscillators under `<multiply unisons="32">`) keeps rendering at full
cost for the entire release - the parent `EnvelopeFilterState` reaching
`isDone()` is the *only* thing that currently ever frees that whole
subtree early (its children are destroyed with it, same "owned by the
parent" pattern as SF2's modulator children). This makes the fix
arguably more valuable here than for SF2: a custom patch's per-voice
cost can be far higher than a single sample voice's.

**Confirmed out of scope: `ResonantFilter`/`BiquadFilter`** (both also
use `EnvelopeState`, per a repo-wide grep). Checked both - their
envelopes drive filter *cutoff*/coefficients
(`ResonantFilter.cpp:47`: `current_cut = cut_min + envelope_level * ...
* (cut_max - cut_min)`), not amplitude. A fully-closed filter isn't
necessarily silent - applying an audibility floor there would be a
category error (timbre parameter, not gain), not a smaller version of
this fix.

## Step 2 plan

**New shared accessor**: `EnvelopeState::isReleasing() const { return
segment == RELEASE; }` (`EnvelopeState.h`, next to `isDone()`, same
minimal single-purpose style) - written once, serves both consumers.

**New shared constant**: `constants::SILENCE_KILL_FLOOR_DB { -60.0f }`
in `constants.h`, alongside `RENDER_EFFECTSAMPLEBLOCK` - both `SoundFont.cpp`
and `effects/EnvelopeFilter.cpp` already include this header, so it's the
natural existing home for a small tunable both consumers share, rather
than two independently-tunable copies that could drift apart.

**Floor value.** `-60.0f` dB as the default - conservative, comfortably
below typical perceptual audibility for an isolated voice. A higher
floor (e.g. -40dB) is a real, more aggressive alternative specifically
for masking under many simultaneous voices (their combined energy raises
the local perceptual noise floor, so -40dB may be genuinely inaudible
when many voices stack, even though it might be marginally perceptible
in isolation) - not claimed as acoustically final, a tunable starting
point like every other first-pass constant introduced this session.

**Adaptive floor (raising it as live voice count climbs): fixed for this
pass, not adaptive.** Neither `SoundFontVoice` nor `EnvelopeFilterState`
has any visibility into how many other voices are active track-wide or
globally - wiring a live count down into every leaf's render/apply call
would be a real architectural addition, not a tuning knob, and it's
adjacent to voice budgeting (explicitly out of scope). A fixed floor
already directly solves the stated problem; revisit only if measurement
later shows pile-up specifically under heavy simultaneous-voice load.

**Where the check runs, per consumer:**
- `SoundFontVoice::render()` (`SoundFont.cpp`) - right after `dryGainMono`
  is computed (`:1323`):
  ```
  if (ampenv_.isReleasing() && gainToDecibels(dryGainMono) < constants::SILENCE_KILL_FLOOR_DB) {
    ampenv_.nextSegment(EnvelopeState::RELEASE);   // -> DONE, same as natural completion
    modenv_.nextSegment(EnvelopeState::RELEASE);   // keep both envelopes consistent - matches killNote()'s existing "always move both together" precedent; modenv_ doesn't affect isActive() itself
  }
  ```
- `EnvelopeFilterState::applyEffect()` (`effects/EnvelopeFilter.cpp`) -
  right after `envelope_state_.process(blockSamples)` (`:37`):
  ```
  if (envelope_state_.isReleasing() && gainToDecibels(gain) < constants::SILENCE_KILL_FLOOR_DB) {
    envelope_state_.nextSegment(EnvelopeState::RELEASE);
  }
  ```
  No second envelope to keep in sync here.

**Cost**: `isReleasing()` is a single bool comparison, checked first and
short-circuiting - the vast majority of active voices at any moment
(held notes in `ATTACK`/`DECAY`/`SUSTAIN`) pay nothing beyond that one
read. Only a voice already in `RELEASE` pays the `log10` call, once per
64-sample block - negligible next to the rest of that same per-block
loop (LFO/modulator evaluation, filter coefficient updates for SF2;
per-channel multiply for EnvelopeFilter).

**Never fires outside RELEASE - the one correctness-critical part.**
`dryGainMono`/`gain` can be arbitrarily low during `ATTACK` (ramping up
from 0) or a deliberately quiet `SUSTAIN` (a soft patch's sustain level,
or a percussive patch's near-zero sustain) while the note is still being
actively held - gating strictly on `isReleasing()` is what prevents
killing a note the player hasn't released yet, regardless of how quiet
its sustain level is. This is not an optimization; it's what makes the
feature safe to add at all.

**Edge cases confirmed handled:**
- A voice already in `fastRelease()`'s ~10ms fast release (retrigger
  cutoff) - this check would still evaluate during that window, but it's
  a no-op in practice: the fast release completes via its own timer well
  before enough 64-sample blocks pass for a -60dB threshold to matter at
  10ms. No conflict, same "idempotent, whichever finishes first wins"
  property already established between `fastRelease()`/`stopNote()`.
- Looping SF2 regions - irrelevant; looping affects `sourceSamplePosition_`/
  sample playback, not the amplitude envelope's segment/level.
- `parameters.release_ <= 0` (already using `TSF_FASTRELEASETIME`'s 10ms
  fallback) - already the fastest release in the system; this feature's
  value is specifically for patches with long *authored* releases.
- `EnvelopeFilterState`'s expensive child chains (unison stacks etc.) -
  freed the same way as everything else: forcing the envelope to `DONE`
  makes `isActive()` false, `clearFinishedVoices()` reaps the parent,
  and the whole child subtree is destroyed with it - no separate
  child-teardown logic needed.

**Files touched:**
- `EnvelopeState.h` - `isReleasing()` accessor.
- `constants.h` - `SILENCE_KILL_FLOOR_DB`.
- `SoundFont.cpp` - the check inside `SoundFontVoice::render()`.
- `effects/EnvelopeFilter.cpp` - the same check inside
  `EnvelopeFilterState::applyEffect()`.

## Verification

1. `cmake --build build -j` clean, no new warnings.
2. `ctest --test-dir build --output-on-failure` 100% pass.
3. New unit tests (reusing `tests/SF2ModulatorTests.cpp`'s
   `writeMinimalSf2()` fixture builder for the SF2 side):
   - A releasing SF2 voice with a long authored release (e.g. the
     existing 1.0s `GenSpec{38, 0}` pattern) whose gain is already below
     the floor at the moment `stopNote()`/`fastRelease()` is called
     (e.g. a very low velocity, or a heavily attenuated region) becomes
     inactive within a handful of 64-sample blocks, not anywhere close
     to its full authored release time.
   - A held (not-yet-released) SF2 voice with a deliberately low
     sustain level below the floor stays active indefinitely - confirms
     the release-only gate (never kills during `SUSTAIN`).
   - A releasing SF2 voice whose gain is *above* the floor keeps running
     its normal release (not killed early) until it naturally crosses
     the threshold or completes.
   - Equivalent held/releasing/quiet-sustain cases for
     `EnvelopeFilterState`, using a minimal `<envelope>`-equivalent
     construction (check whether `EnvelopeFilter`/`EnvelopeFilterState`
     already has any test coverage to extend, or whether a new small
     fixture is needed).
4. Manual: hold and release a long-tail GM pad and a heavy-unison custom
   patch; confirm `InfoLine`'s voice count drops shortly after the
   sound becomes inaudible rather than lingering for the full authored
   release.
