# Launchpad poly-aftertouch "excess over floor" transform

## Context

The channel-pressure -> SF2 modulator feature (see the (now superseded)
`plans/aftertouch-poly-pressure.md` for its original design, since
implemented) turned out to be nearly unusable on the Launchpad in
practice: hitting a note with high velocity immediately reads as high
channel pressure too, forcing vibrato/filter movement the player never
asked for, and there's no way to hit hard *without* triggering it - only
a soft hit (which also means a quiet note) avoids the problem.

**Root cause.** Pad-style controllers like the Launchpad almost
certainly use a single continuous force sensor (FSR or similar) per pad,
not two independent sensors the way a real weighted keybed separates
strike-velocity timing from a dedicated aftertouch strip. Velocity is
derived from how fast that single sensor's reading rises during the
initial contact; poly aftertouch is the same sensor's continuing reading
for as long as the pad stays held. Because both numbers come from the
same underlying signal, a hard hit's aftertouch reading starts out
already close to the strike velocity - there is no hardware-level way to
tell "hit hard, now pressing lightly" apart from "hit hard, still
pressing hard" in the first instant after contact. Switching to real
MIDI channel pressure (`CHANPRESS`, as opposed to per-pad poly pressure/
`KEYPRESS`) would not fix this - it's very likely derived from the same
per-pad sensors (aggregated, typically as a max across held pads), so it
has the identical correlation problem, and would additionally lose
per-note distinction for chords. Not investigated against real hardware
documentation - flagged as a reasonable inference, not a confirmed fact.

**The fix, agreed on across this planning conversation**: transform raw
poly-aftertouch readings into "excess pressure over a per-note floor"
before they ever reach the SF2 channel-pressure modulator or get
recorded into the pattern - simulating what an instrument with genuinely
independent velocity/aftertouch sensors would produce, as closely as a
single conflated sensor allows.

## Design (fully agreed)

**Per-note floor, seeded from that note's own strike velocity.**
`LaunchpadManager::ActiveNote` (`LaunchpadManager.h`) gains a
`pressure_floor` (and a smoothed-pressure state, see below), initialized
at `PRESS` time to the note's own velocity (the same value already used
for `Note`'s stored velocity and the `PLAY_NOTE` event) - not 0. This is
what stops an immediate post-strike aftertouch reading (which is close
to the strike velocity) from reading as significant "excess."

**Floor only ever ratchets down, never up, and only for the lifetime of
that one held note.** Every `AFTERTOUCH` event: if the (smoothed) reading
is below the current floor, the floor drops to match. A fresh press
always starts a fresh floor at that press's own velocity - no state
persists across notes, no explicit reset code needed beyond
`recordActiveNote()` already replacing the whole `ActiveNote` on each new
press.

**Excess, normalized to fill the remaining headroom.** `available = 127 -
floor`; `excess = current - floor` (always >= 0, since floor never
exceeds current thanks to the ratchet); `normalized = excess / available`
(or `0` if `available <= 0`, i.e. the note was struck at the sensor's
max, leaving no headroom at all - see the "known limitation" section
below, no cap is being added for this). `normalized` is scaled back to
an integer `0..127` (`lround(normalized * 127)`, clamped) purely to keep
using the existing `PlaybackControlEvent`/pattern-storage plumbing
unchanged - see "float vs int" below.

**Low-pass filter (EMA) on the raw readings, applied before both the
floor comparison and the excess numerator** - agreed on to address
finger tremor / sensor noise, which would otherwise let a single noisy
low reading permanently drop the floor. `smoothed += alpha * (raw -
smoothed)`, seeded to the note's own velocity at press (matching the
floor's own seed). Both the floor-ratchet check and the excess
calculation use `smoothed`, not `raw` - smoothing only the floor and
leaving the numerator raw would still let a noisy numerator make the
final modulation amount itself jittery. `alpha` is a first-pass tunable
constant (starting guess ~0.3, expect to retune by feel on real
hardware, consistent with how the vibrato/filter-cutoff heuristic amounts
from the earlier plan were also flagged as starting points, not final).
MIDI aftertouch messages arrive irregularly (event-driven, not a fixed
clock) - a plain fixed-`alpha` EMA filters slightly inconsistently
depending on message spacing; explicitly decided this isn't worth solving
with real elapsed-time-based filtering for a control signal at this rate.

**Explicitly rejected: time-based floor "stiffening"/freeze.** Considered
and rejected a design where the floor becomes progressively harder to
move the longer a note is held (effectively frozen after ~10s). Reasons,
from this conversation:
1. A real separate-sensor instrument's aftertouch doesn't get less
   responsive over time - this would be a step away from realism, not
   toward it.
2. The EMA smoothing above already covers the actual noise-rejection
   concern this was aimed at.
3. It would break a legitimate long-hold performance gesture: slowly
   easing off a hard-struck pad note over many seconds, then pushing
   again later - a frozen floor would stop tracking that real, deliberate
   change.

If testing on real hardware shows smoothing alone still isn't enough
(e.g. genuine tremor large enough to punch through the EMA), the
fallback ideas discussed (not yet needed/agreed) are a minimum-margin
threshold before the floor is allowed to move (e.g. must drop >= 3-5
units, not just any amount) or a bounded-per-second descent *rate* on the
floor - deliberately not an age-based stiffening, since a rate limit
keeps the same responsiveness for the whole life of the note rather than
changing behavior based on how long it's been held.

**Recorded pattern data stores the already-transformed (excess) value,
not the raw hardware reading.** Explicit decision: the goal is to
simulate "what would this instrument's own separate aftertouch sensor
have reported," so the *transformed* number is the thing worth
preserving - replaying the pattern later should reproduce the exact
modulation depth the performer felt live, without needing playback to
redo any floor/smoothing logic of its own (which would require carrying
floor state through the whole render pipeline, not just at the Launchpad
input boundary - substantially more invasive, and not needed since the
transform is deterministic and can be fully "baked" into the recorded
number once, at input time).

**Float internally, int at the storage/event boundary.** Floor/smoothed-
pressure state is tracked as `float` inside `LaunchpadManager` (no reason
to re-quantize mid-computation), but the final value that reaches
`PlaybackControlEvent::NOTE_PRESSURE` and gets written into a `Note`'s
velocity field stays a plain `0..127` integer - matching every other
velocity/pressure number already flowing through that plumbing, and
because the tracker UI displays note data as integers (explicitly
confirmed: storing genuine floats there was considered and rejected).

**Known, accepted limitation - no mitigation planned.** A note struck at
maximum velocity (127) leaves zero headroom (`available = 0`), so no
amount of further pressure can register as excess for that note. This is
an information-theoretic consequence of deriving two signals (impact
speed, continuing force) from one bounded sensor - a real instrument's
independent aftertouch sensor wouldn't have this limitation, since it
never shares dynamic range with the velocity measurement. A mitigation
(capping the floor below the true max, e.g. at 100, to always guarantee
some headroom even after the hardest possible hit) was discussed and
explicitly declined ("No cap") - a max-velocity hit simply gets no
further modulation, accepted as reasonable rather than worth the loss of
realism the cap would introduce.

## `delayVibLFO` interaction (background, not a design change)

Raised during planning: `delayVibLFO` is a pre-existing, unrelated SF2
generator (already implemented, long before this session's channel-
pressure work) controlling how long the vibrato LFO itself waits after
note-on before oscillating at all. The channel-pressure vibrato
contribution (`SoundFontVoice::render()`'s `cpVibLfoToPitch`) rides on
the *same* LFO oscillator as a patch's own static vibrato amount
(`viblfo_.getLevel() * (staticAmount + channelPressureAmount)`), so a
patch with a large `delayVibLFO` would gate our pressure-driven vibrato
too, independent of this plan's floor/excess fix. Confirmed the SF2 spec
default is effectively 0 seconds (parsed value `-12000` timecents clears
to `0.0f`), so this shouldn't matter unless a specific patch deliberately
authored a nonzero delay - worth checking on the actual preset in use
(e.g. DrawbarOrgan) if vibrato still doesn't appear after this plan's fix
lands.

## Implementation sketch

Everything here is scoped to `LaunchpadManager.h`/`.cpp` only - explicitly
NOT touching `PlaybackControlEvent`, `Player.cpp`, `MidiEvent.h`, or the
regular-MIDI-keyboard `PatternEditor.cpp` NOTE_PRESSURE path, since the
Launchpad's own pad-pressure handler is entirely separate from that
regular-MIDI path and is where this kind of filtering belongs.

- `LaunchpadManager::ActiveNote` (`LaunchpadManager.h`): add `float
  pressure_floor = 0.0f;` and `float smoothed_pressure = 0.0f;` (both
  seeded from strike velocity at `PRESS` time, not defaulted to 0 in
  practice - the `= 0.0f` here is just the aggregate-init fallback,
  mirroring how `last_aftertouch_value`'s existing `= -1` default already
  works alongside the 3-positional-argument `recordActiveNote(...,
  {note_column, row, track_id})` call site).
- `LaunchpadManager::handlePadEvent()`'s `PRESS` branch
  (`LaunchpadManager.cpp`): reorder so `velocity` is computed *before*
  `recordActiveNote()` (currently the reverse), and pass it through as
  both `pressure_floor` and `smoothed_pressure`'s seed.
- `LaunchpadManager::handlePadEvent()`'s `AFTERTOUCH` branch: replace the
  direct `ev.getVelocity()` use (both in the live `NOTE_PRESSURE` push
  and the recorded `note.setVelocity()` call) with the smoothed/floored/
  normalized/rescaled value described above. The existing write-throttle
  (`aftertouch_threshold`/`last_aftertouch_value`) stays comparing raw
  hardware readings, not the transformed value - unrelated concern (rate-
  limiting pattern writes, not the modulation transform itself).
- No changes needed to `SoundFontVoice`, `InstrumentTrackState`, `Player.cpp`,
  or `SF2Modulator.h`/`.cpp` - they already consume whatever `0..127` value
  arrives via `PlaybackControlEvent::NOTE_PRESSURE` exactly as before;
  this plan only changes *what number* Launchpad-sourced aftertouch puts
  into that existing pipe.

## Verification

1. `cmake --build build -j` clean, no new warnings.
2. `ctest --test-dir build --output-on-failure` still 100% pass (no
   existing test exercises `LaunchpadManager`'s pressure transform
   directly yet - consider a small unit test around the pure floor/
   excess math, factored out into a testable free function rather than
   inlined in `handlePadEvent()`, similar to how `LaunchpadLayout.h`/
   `.cpp` keeps pure computation separate from `LaunchpadManager.cpp`'s
   device-facing code).
3. Manual, on real hardware: strike a note hard and hold steady - confirm
   no vibrato/filter movement. Push harder from there - confirm vibrato/
   filter now audibly responds. Ease off partway, then push again from
   that lower point - confirm it registers as fresh excess (floor
   ratcheted down). Strike very softly, then lean into the pad
   progressively - confirm vibrato builds in smoothly. Strike at maximum
   velocity - confirm (expected, accepted limitation) no further
   modulation is possible from that note.
