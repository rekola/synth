# `<tapeDegradation>` - tape/media degradation

Models a physical playback machine - wow/flutter, hiss, dropouts,
saturation - sitting between a source and the listener. It's
source-attached, not a bus effect: see `docs/effects.md` for the
general track-vs-voice attachment mechanic every effect on this page's
element shares. Tape degradation is the one effect where that choice
changes the instrument's *character*, not just its scope:

- **Track-attached** - one machine, running continuously for the whole
  song, independent of which notes are currently sounding. A tape player
  sitting in the room everything on that track plays through.
- **Voice-attached** - one machine *per note*, built fresh at note-on
  and torn down after note-off. Independent wow/flutter/hiss per held
  note, rather than one shared wobble across a whole chord - this is
  what the `mellotron` preset needs, since a real Mellotron's notes each
  come off their own physical tape strip.

```xml
<!-- Track-attached: a tape player in the corner -->
<tracks>
  <tapeDegradation preset="tape" azimuth="-30" distance="2">
    <track id="0" instrument="0"/>
  </tapeDegradation>
</tracks>

<!-- Voice-attached: a Mellotron, one strip per note -->
<instruments>
  <tapeDegradation preset="mellotron">
    <envelope attack="0.01" hold="0.2" decay="0.1" sustain="0.6" release="0.1">
      <oscillator type="saw"/>
    </envelope>
  </tapeDegradation>
</instruments>
```

## Position

`azimuth`/`elevation`/`distance`/`extent` are only read when
track-attached - they're this instance's own position (same meaning as
the identically-named attributes on a plain `<track>`, `docs/effects.md`),
independent of wherever the track it wraps happens to be. Left
unauthored (the default), the machine has no position of its own and
its hiss/wow/etc. render omnidirectional (W-only).

Voice-attached, these four attributes are ignored entirely - the
position is always whatever the note itself was given, so the
degradation stays glued to the note's own point in space.

## Attributes

All XML-only, read once at song load - none of these are live-automatable
(no M-x/UI/Launchpad control reaches them; edit the file and reload to
change one). `preset` (below) supplies every attribute's default; an
explicit attribute always overrides its preset's value.

**Wow & flutter** - slow (wow) and fast (flutter) speed wobble, read
through a cubic-interpolated fractional delay (linear interpolation
aliases audibly on a moving read pointer - see `dsp/FractionalDelayLine.h`'s
`readCubic()`).

| Attribute | Meaning |
|---|---|
| `wowRateHz` | Wow rate, Hz. |
| `wowDepthCents` | Wow depth, ± cents. |
| `wowLocked` | `true` locks wow to a fixed, deterministic rate (`wowLockedRateHz`) instead of the health-driven random kind - a turntable's rotation doesn't wander with the transport's own "mood" the way a tape deck's does. |
| `wowLockedRateHz` | Locked wow rate, Hz - `0.5556` (33⅓rpm) for the `vinyl` preset. |
| `flutterRateHz` | Flutter rate, Hz. |
| `flutterDepthCents` | Flutter depth, ± cents. |

**Transport health** - one slowly-wandering value drives every fault
below in correlation, so a bad moment for the machine shows up as a
wow/flutter widening, hiss rising, and a dropout/click becoming more
likely all at once, rather than as independent coincidences.

| Attribute | Meaning |
|---|---|
| `healthRateHz` | How fast health wanders. |
| `healthSensitivity` | How strongly health scales everything else - 0 decouples all the fault components from health entirely (each still happens, just always at its nominal rate/depth). |

**Hiss, dropouts, clicks, rumble**

| Attribute | Meaning |
|---|---|
| `hissLevelDB` | Broadband hiss level. |
| `hissLevelDependent` | 0.0-1.0 - scales hiss with the input's own level (envelope-followed) on top of `hissLevelDB` - level-dependent grain noise, `opticalFilm`'s defining trait. 0 (default) makes hiss level-independent. |
| `dropoutRateHz` | Poisson mean rate of gain-dip dropouts, events/s, at health = 1 (nominal). |
| `dropoutDepthDB` | Gain during a dropout. |
| `dropoutDurationMs` | Dropout duration. |
| `clickRateHz` | Poisson mean rate of filtered impulse clicks, events/s, at health = 1. |
| `clickGainDB` | Click peak level. |
| `rumbleLevelDB` | Low-frequency rumble level - a second, independent noise source from hiss, one-pole low-passed at `rumbleHz` rather than pink-filtered (motor/bearing noise and subsonic warp, not a broadband tilt). Defaults far enough down to be inert; `vinyl` raises it. |
| `rumbleHz` | Rumble's own lowpass cutoff. |

**Tone**

| Attribute | Meaning |
|---|---|
| `saturationDriveDB` | Soft-saturation drive - health-scaled, so trouble bites harder here too. |
| `lowCutHz` | Low-end rolloff - narrows the band (Cassette/Mellotron/Dictaphone). Near-inaudible by default. |
| `hfRolloffHz` | High-end rolloff. |
| `headBumpHz` / `headBumpGainDB` | A peaking boost around the tape head's own low-frequency resonance. |
| `breathingAmount` | 0.0-1.0 - modulates `hfRolloffHz`'s own cutoff with the input's envelope (Dolby-style HF breathing, `cassette`'s defining trait: the top end audibly opens on loud passages, dulls on quiet ones). 0 (default) is a fixed cutoff. |
| `breathingAttackMs` / `breathingReleaseMs` | Envelope-follower timing shared by `breathingAmount` and `hissLevelDependent` above - one follower, two possible destinations. |
| `mix` | Dry/wet, 0.0-1.0. Not a "how present is the machine" knob so much as a fade control - e.g. starting a song fully degraded and cleaning up over the arrangement, or the reverse. |

**Mellotron-only** (voice-attached use) - independent per-note drift/hiss
already falls out of voice attachment for free; these are the other
three traits that make a Mellotron read as one: the short attack
pitch-swoop into tune, narrow band (`lowCutHz`/`hfRolloffHz` above), and
pressure-pad amplitude flutter. Also the note-off half of the same
shape, spin-down, relevant to any voice-attached preset even though only
Mellotron sets a nonzero `droopDepthCents` by default:

| Attribute | Meaning |
|---|---|
| `ampFlutterDepth` | 0.0-1.0 - a slow AM tap riding the same flutter phase as the pitch flutter above (one mechanical wobble, two audible symptoms). 0 (default) is inert. |
| `swoopStartCents` | Pitch offset at the very start of a note, decaying to 0 - "coming up to speed." 0 (default) is inert. |
| `swoopTimeMs` | How long the swoop-in takes. |
| `spinDownMs` | How long note-off's spin-down takes - deliberately its own parameter, not derived from the instrument's own release time (see "Note-on/note-off shaping" below): a short release with a short spin-down gives the Mellotron feel, a long release with a late spin-down gives a pad that sags only at the very end. |
| `droopDepthCents` | Pitch droop by the end of spin-down - 0 gives a clean stop with no pitch bend, still a fully faded one. |

**Disintegration-only**

| Attribute | Meaning |
|---|---|
| `decayMode` | `true` makes health's usual ceiling drift monotonically downward over playback time instead of staying fixed at 1.0 - material erodes and never recovers. `false` (default) makes `decayRatePerMinute` irrelevant. |
| `decayRatePerMinute` | Health units lost per minute of program time. Tracks the effect instance's own elapsed render time (not wall-clock, not a raw sample count - stays meaningful regardless of render sample rate, and reproducible for `--render`/regression tests), reset whenever the song reloads. |

## Presets

| Preset | Attachment | Defining trait |
|---|---|---|
| `tape` (default) | either | Base character - moderate wow/flutter/hiss/dropouts, no genre-specific machinery engaged. |
| `mellotron` | voice | Pronounced wow/flutter, narrow band, attack swoop, pressure-pad amplitude flutter, short spin-down. |
| `studio` | either | 15/30ips studio machine - every fault turned down close to inaudible, kept mainly for the saturation/head-bump warmth a real deck still imparts even running well. |
| `cassette` | either | Narrow band, Dolby-style HF breathing tied to input level (`breathingAmount`). |
| `vinyl` | track | Locked 33⅓rpm wow instead of random, a dense click field, audible low rumble. |
| `disintegration` | either | Health becomes a one-way decay - material audibly erodes over playing time. |
| `dictaphone` | either | Narrow band + more hiss + more flutter, taken to a dictation machine's extreme; a mild amount of breathing too (cheap dictaphones commonly had rudimentary automatic level control). |
| `opticalFilm` | either | Level-dependent grain noise (`hissLevelDependent`) - optical soundtrack grain genuinely tracks how much light/signal is present, unlike magnetic tape hiss. Wow/flutter here models sprocket-hole irregularity rather than a tape transport's. |

Every preset shares the same parameter set and the same code path - what
makes each one distinct is purely which attributes it moves away from
`tape`'s baseline, the same "preset vs. explicit attribute" override
rule as the bus effects (`docs/bus_effects.md`): `preset="mellotron"
wowDepthCents="10"` means "the Mellotron preset, but with a subtler
wow." An unrecognized `preset` name falls back to `tape`.

## Note-on/note-off shaping (voice-attached)

Voice-attached, note-on and note-off drive an explicit state machine
(`Stopped -> SpinUp -> Running -> SpinDown -> Stopped`) independent of
whatever amplitude envelope the wrapped instrument has - this never
wraps or duplicates the instrument's own ADSR, it only shapes the *tape
machine's* own spin-up/spin-down character:

- **SpinUp** (note-on): `swoopStartCents` fades to 0 over `swoopTimeMs`.
- **Running**: steady state, everything above as authored/health-modulated.
- **SpinDown** (note-off): hiss/dropouts/click/amplitude-flutter fade to
  nothing and pitch droops toward `-droopDepthCents`, both over
  `spinDownMs` - not derived from the instrument's own release time, so
  a short release with a short spin-down gives the classic Mellotron
  feel, while a long release paired with a late spin-down gives a pad
  that only sags at the very end.
- **Stopped**: no further pitch modulation or new hiss/dropouts - the
  wow/flutter delay line simply finishes draining whatever it was still
  holding, so the voice can be reclaimed without truncating audio that
  was already written into it.

This is also why a voice never cuts off the instant its note ends: the
voice stays resident for as long as spin-down is still fading *or* the
delay line still has content to drain, then is freed - never earlier
(a click) and never indefinitely (track-attached is the only mode meant
to run forever).

## Not yet implemented

- **Per-chord azimuth spread (`mellotron`)**: every note currently
  shares its track's own position exactly, so a held chord's
  independently-drifting tape strips don't also read as physically
  separate points in space the way a real Mellotron's side-by-side tape
  heads would. `NoteMultiplier`'s own unison spread (`NoteMultiplier.cpp`,
  `atan2f(spread * position.extent, position.distance)` for an angular
  half-width) is the closest existing precedent, but doesn't transfer
  directly - it spreads a known, fixed unison count within one
  `playNote()` call, whereas a Mellotron chord's notes each arrive via
  their own separate `playNote()` with no visibility into what else is
  currently held.
- **Live control**: every attribute on this page, `preset` included, is
  XML-only, read once at song load - no automation path (M-x/UI/
  Launchpad) reaches any of them yet. Worth revisiting if there's ever a
  reason to want e.g. `mix` faded live during playback rather than only
  pre-authored.
