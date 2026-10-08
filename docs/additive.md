# `<additive>` - decaying-partials string instrument

An `Instrument` leaf, like `<oscillator>`/`<padsynth>`: it goes inside
`<instruments>`, almost always wrapped in an `<envelope>` (`docs/effects.md`).
A note is a set of decaying sinusoids, one set per string, built at note-on
and living as long as the voice.

```xml
<instruments>
  <envelope attack="0.005" decay="8.0" sustain="0.0" release="0.3">
    <additive preset="piano"/>
  </envelope>
</instruments>
```

The model (`AdditiveModel.h`) decides which partials a note has: frequency,
starting level, decay rate and start phase, and which string each belongs to.
The engine (`SinusoidBank.h`) runs them. The per-partial decay is a *timbral*
effect, the note's brightness fading as it ages; the parent `<envelope>` still
governs overall amplitude.

## Tuning

With `tuningMatched` (the default) every partial sits on the nearest step of
the song's tuning, `2^(round(N·log2 n)/N)` for `N` steps per octave, so the
partials two chord tones share meet exactly when the interval is one of the
tuning's own. In 31-EDO, partial 3 is 5.2 cents flat of 3:1, and the shared
partials of 7:6, 8:7, 7:4 and 12:7 (the lower note's 7th, 8th, 7th and 12th)
land on the same step as the upper note's 6th, 7th, 4th and 7th. `N` is read
from the song's current tuning when the note starts; `Tuning::PERCUSSION` has no
steps, so partials are plain harmonics there.

`stretch` (ε) adds a small, bounded inharmonicity on top: partial n sits at
`grid(n) + (n-1)·ε` times the fundamental, the same form the FM electric
pianos get from a modulator at 1+ε. The fundamental is never moved, partial n
is at most about `1200·log2(1+ε)` cents above its grid position (1.7 cents at
ε = 0.001), and the partials two notes share differ by at most about ε times the
lower note's fundamental for intervals up to an octave. A stiff string's
`n·sqrt(1+B·n²)` stretch is not used: it would detune exactly the partials
septimal intervals meet at.

## Strings and space

`unisonVoices` (1-3) strings per note, `unisonDetune` cents apart (centred, so
the note's pitch is exact; a small hashed jitter keeps the copies from beating at
a mechanical rate). Each string is its own output row, placed at its own
direction and all encoded in one pass: `keyboardSpread` degrees of azimuth
across the keyboard (bass left, treble right, over an 88-key span centred on
middle C) and `stringSpread` degrees between a key's adjacent strings, both 0 by
default. The noise burst sits at the key's position.

## Attributes

| Attribute | Meaning |
|---|---|
| `preset` | Below. Supplies every other attribute's default; an explicit attribute overrides it. An unknown name is `default`. |
| `partials` | Partials per string (those past Nyquist are skipped). |
| `tuningMatched` | `false` leaves partials at plain harmonics (plus `stretch`). Default `true`. |
| `stretch` | ε above. |
| `tilt` | Spectral tilt, dB/octave: `amplitude_n = 10^(tilt·log2(n)/20)`. |
| `velocityTilt` | Added tilt per unit of velocity above 0.5, so a harder hit is brighter. |
| `unisonVoices` / `unisonDetune` | Strings per note and their spacing in cents. |
| `keyboardSpread` / `stringSpread` | Degrees of azimuth, above. |
| `decayA` / `decayB` / `decayP` | `α_n = decayA + decayB·f_n^decayP`, nepers/second: `amplitude(t) = amplitude(0)·exp(-α_n·t)`. Higher partials die first. |
| `attackNoiseLevel` | Level of a ~15 ms noise burst for the hammer. 0 is off. |
| `level` | Output gain multiplier. Default 1. |

## Engine

Each partial is a coupled-form recursive oscillator (`y[n] = 2·cos(w)·y[n-1] -
y[n-2]`), with its state in flat parallel arrays and eight partials per vector
lane group. A partial above Nyquist is never added; one 90 dB below its own
starting level is culled from its string (checked once per `render()`), since
decay is monotonic. Start phases come from `dsp::HashField` and are the same
for a given `NoteCoordinate` on every platform.

Measured on one machine (Release, 48 kHz, 1024-frame blocks, the voice chain
with the ambisonic encode, 3 strings × 28 partials): a fresh voice costs about
43 µs a block and 32 simultaneous voices about 1.5 ms in total, 7% of the 21.3
ms block budget. `SYNTH_TIMING=1 SYNTH_TIMING_PRESET=piano ./build/tests/synth_tests`
prints the figures.

## Presets

| Preset | Character |
|---|---|
| `default` | A plain struck string: one string, harmonic partials on the tuning, mild tilt, moderate decay. |
| `piano` | Three strings a cent apart, 28 partials on the tuning with `stretch` 0.001, a short noise burst. The library's `piano.acoustic.grand` (used when the SoundFont has no piano). |

## Not yet implemented

- No live/automatable control of any attribute.
