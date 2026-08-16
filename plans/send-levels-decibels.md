# Convert track send levels (Send Main/A/B) to decibels

## Context

`InstrumentTrack`'s three send levels (`SendLevels{main,a,b}`, `SendLevels.h`)
are plain linear multipliers, 0.0-1.0, edited today only via the Launchpad's
per-track fader rows (`LaunchpadManager::handleRawButton`/grid-fader path,
`controller.setTrackSendA(track_id, static_cast<float>(ev.getY()) / 7.0f)` -
`LaunchpadManager.cpp:706-708` - and the equivalent for B/Main) and persisted
as a raw fraction in `songs/*.xml` (`sendA`/`sendB`/`sendMain` attributes,
`InstrumentTrack.cpp`). A linear fader over a perceptual (~logarithmic)
loudness quantity wastes most of its travel/resolution at the loud end and
crams all the useful range into the bottom eighth - hard to dial in a subtle
send. Renoise's own mixer send/volume faders are dB-scaled with a hard
silence floor at the bottom, not linear amplitude - the precedent this plan
follows.

The codebase already has the needed conversion, used the same way for
`Amplifier`'s per-track gain and every instrument's `getGainDB()`
(`OscillatorVoice`, `Noise`, `SoundFont`, `FileInstrument`):
`TreeNode::gainToDecibels()`/`decibelsToGain()` (`model/TreeNode.h:67-73`),
with `gain <= .00001 -> -100dB` and `db <= -100 -> gain 0` as the established
"off" floor. This plan reuses that convention rather than inventing a new
one.

## Design

**dB is the unit at the control-surface and file-format boundary only; the
DSP-facing representation stays linear, unchanged.** `SendLevels{main,a,b}`
is read directly, per sample, in the audio callback
(`InstrumentVoice.h:226-229`: `aux_a[i] = dry[i] * sends.a;`) - it must stay
a plain linear scalar there, not something requiring `pow()`/`log10()` per
sample or even per block. `InstrumentTrack::sends_`, `createState()`,
`InstrumentTrackState`'s `setSendA/B/Main` -> `VoiceState::adjustSendA/B/Main`
push-to-active-voices path, and `SoundFont.cpp`'s region-combine math
(`combineRegionSendA`, `chorusSendFor`) are all untouched by this plan - they
keep exactly their current linear contract.

A second, independent reason this must stay linear rather than becoming dB
end-to-end: `SoundFont.cpp`'s `combineRegionSendA()` *sums* the track's own
Send A with a SoundFont region's own authored `reverbEffectsSend` generator
(some GM patches bake in their own reverb wetness), additive-then-clamped -
mirroring the SF2 spec's own generator-merge convention (region + preset
offsets, summed then clamped), and matching the intuitive reading of "the
track sends 30%, this patch wants 20% more, so 50% total, clamped at 100%".
That combination is only correct - both audibly and per the SF2 convention
it mirrors - as a linear-domain sum: dB addition is linear
*multiplication*, so summing in dB would silently turn "add this patch's
own send on top of the track's" into something that gets *quieter* as
either contribution increases past unity, the opposite of the intended
effect. Storing dB pervasively would mean converting back to linear right
before this sum anyway (and the chorus tap sends just below it,
`aux_a[i] += 0.5f*(wetL[i]+wetR[i])*sends.a`) - strictly more conversions
than the current design, not fewer, for no behavioral benefit.

The conversion happens at exactly two boundaries, both already control-rate
(note-trigger/knob-edit/song-load), matching how `Amplifier`/`getGainDB()`
already call `decibelsToGain()` at the point of use rather than storing a
pre-converted value:

1. **`Controller::setTrackSendA/B/Main(int track_id, float value)`** - the
   one entry point every control surface (today: Launchpad; tomorrow:
   any on-screen fader/numeric editor) goes through. `value` is now in dB;
   each function converts to linear via a small local `dbToLinear()` before
   calling `instrument_track->setSendA(linear)` (unchanged signature/
   contract) - self-contained rather than reaching into
   `TreeNode::decibelsToGain()`, which only `TreeNode<Derived>` subclasses
   (`TrackState`/`VoiceState`) can reach; `Controller` is neither. This is
   the same "each file keeps its own small dB helper" convention
   `effects/Compressor.cpp`/`dsp/TapeTransport.cpp`/`effects/
   TapeDegradation.cpp`/`bus/Haze.cpp` already use, including the same
   `-100dB` floor. `PlaybackControlEvent`'s fixed-point encoding
   (`static_cast<int>(linear * 1000.0f + 0.5f)`) is unchanged - it still
   carries the already-linear value, so `Player.cpp`'s decode side needs no
   change at all.
2. **`InstrumentTrack::loadParameters()`/`storeParameters()`** - the XML
   boundary, via its own local `dbToLinear()`/`linearToDb()` pair (same
   convention/floor as above). See migration below for why this keeps the
   existing attribute names rather than introducing new ones.

**Floor/range**: reuse `-100dB == off` exactly as `TreeNode` already defines
it - no new constant. No hard ceiling above 0dB (matches `Amplifier`'s own
unclamped `gain_`); 0dB remains unity/"full send", same ceiling the old
linear 1.0 gave.

### XML migration - same attribute names, one-time value rewrite

`sendA`/`sendB`/`sendMain` keep their existing attribute names but change
meaning from a linear 0-1 fraction to dB. Given how few songs actually carry
these attributes (checked: `demo3.xml`, `demo12.xml`, `demo13.xml`,
`songtest1.xml`, `songtest7.xml`, `songtest15.xml`, `padtest1.xml`,
`padtest2.xml`, `fm_test1.xml`, `haze_demo.xml`, `arptest1.xml`,
`arptest2.xml`, `a.xml`), a one-time rewrite of every existing value (old
linear fraction -> `gainToDecibels(fraction)`) is simpler than carrying a
permanent dual-attribute fallback for a handful of files. `loadParameters()`/
`storeParameters()` read/write `sendA`/`sendB`/`sendMain` as dB, unconditionally,
with no legacy-linear fallback path.

The rewrite touches `songs/*.xml` content directly (a one-time pass over the
files listed above, converting only the `sendA`/`sendB`/`sendMain` attribute
values, nothing else in the file) - kept in its own commit, separate from
the code change, and never bundled with any of the user's own pre-existing
uncommitted song edits (some songs already have unrelated uncommitted
changes at plan time).

### Launchpad fader curve

`LaunchpadManager.cpp`'s Send A/B/Main fader rows used to map `Y` (0-7)
linearly to `Y/7.0f`. New curve (`LaunchpadManager::sendRowToDb()`/
`sendLinearToRow()`, the same static-stateless-pair convention
`azimuthToRow()`/`rowToAzimuth()` already establishes for the Pan row, so
the button-press handler and `refreshLeds()`'s own bargraph readback always
agree), matching the "bottom position is a hard off" convention real mixer
faders use (not just a very quiet dB value):

- `Y == 0` -> off (`-100dB`, which the local `dbToLinear()` collapses to
  exactly `0`).
- `Y == 1..7` -> linearly spaced in dB from a floor to `0dB` at `Y == 7`.
  Floor: `-36dB` (six 6dB steps, `Y=1 -> -36dB, Y=2 -> -30dB, ..., Y=7 ->
  0dB`) - 6dB/step is a standard, easily-perceived mixing-console
  increment.

## Files touched

- `Controller.h`/`Controller.cpp` - `setTrackSendA/B/Main`: doc comments
  updated (value now dB); local `dbToLinear()` converts before calling the
  model setter and building the (unchanged) `PlaybackControlEvent`.
- `model/InstrumentTrack.cpp` - local `dbToLinear()`/`linearToDb()`;
  `loadParameters()`/`storeParameters()` read/write `sendA`/`sendB`/
  `sendMain` as dB (no fallback - see migration above).
- `model/InstrumentTrack.h` - doc comment on `setSendA`/`setSendB`/
  `setSendMain`/`getSends()` (still linear; dB is a layer up).
- `songs/*.xml` - one-time value rewrite of existing `sendA`/`sendB`/
  `sendMain` attributes (linear -> dB, via a throwaway migration script),
  own commit.
- `launchpad/LaunchpadManager.h`/`.cpp` - new `sendRowToDb()`/
  `sendLinearToRow()` static pair (plus a local `linearToDb()`); the three
  fader-press call sites and `refreshLeds()`'s bargraph readback now go
  through them instead of the old `Y/7.0f`/`value*7.0f` linear mapping.
- `SendLevels.h` - doc-comment update only (clarify it's linear, and
  *where* the dB conversion actually happens - no field/type change).
- `tests/fixtures/*.xml` - `send_a_oscillator.xml`, `send_b_oscillator.xml`,
  `haze_oscillator.xml`, `send_a_distance_near/far.xml`,
  `bus_chain_send_on/off.xml`, `send_main_zero.xml`,
  `tape_degradation_send_a.xml`: `sendA`/`sendB="0.5"` -> `"-6.0206"` (same
  linear gain, so existing assertions keep meaning what they meant);
  `send_main_zero.xml`'s `sendMain="0.0"` -> `"-100"` (old value meant
  linear-zero/off; under dB semantics `"0.0"` means unity, the opposite -
  `"-100"` is the actual hard-off value now).
- `docs/commands.md` - none expected (no pattern-effect command touches
  sends today).

## Verification

- `--render` a migrated song, confirm it plays/builds without error and
  the rewritten dB value reproduces the original linear gain
  (`linearToDb(dbToLinear(old_fraction_in_db)) == old_fraction_in_db`
  within tolerance) so the migration is audibly a no-op.
- Full existing suite stays green (`ctest --test-dir build`), including the
  fixture-based Render/BusEffectRegistry tests above, updated to the new
  dB values.

## Constraints honored

- No change to the per-sample audio callback path or its types -
  `SendLevels`, `VoiceState::adjustSendA/B/Main`, `InstrumentVoice`,
  `SoundFont.cpp` region-combine math all keep today's linear contract
  exactly.
- The `songs/*.xml` value rewrite touches only the `sendA`/`sendB`/
  `sendMain` attribute values, nothing else in each file, and lands in its
  own commit, never bundled with the code change or with any of the user's
  own pre-existing uncommitted song edits.
