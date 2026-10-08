# `<additive>` - struck and plucked strings from decaying partials

An `Instrument` leaf, like `<oscillator>`/`<padsynth>`: it goes inside
`<instruments>`, almost always wrapped in an `<envelope>` (`docs/effects.md`).
A note is a set of decaying sinusoids, one set per string, built at note-on and
living as long as the voice. The model is a string (or bar) excited by a hammer
or a pluck, ringing with decays that depend on the key, next to a body that
answers every note the same way.

```xml
<instruments>
  <envelope attack="0.002" decay="8.0" sustain="0.0" release="0.3">
    <additive preset="piano"/>
  </envelope>
</instruments>
```

`AdditiveModel.h` decides which partials a note has (frequency, starting level,
decay, start phase, which string); `SinusoidBank.h` runs them. The per-partial
decay is a *timbral* effect, the note's brightness fading as it ages; the parent
`<envelope>` still governs overall amplitude, so give a piano a long decay stage
and `sustain="0"` rather than letting the envelope shorten the strings.

## Tuning

With `tuningMatched` (the default) every partial sits on the nearest step of the
song's tuning, `2^(round(N·log2 n)/N)` for `N` steps per octave, so the partials
two chord tones share meet exactly when the interval is one of the tuning's own.
In 31-EDO, partial 3 is 5.2 cents flat of 3:1, and the shared partials of 7:6,
8:7, 7:4 and 12:7 (the lower note's 7th, 8th, 7th and 12th) land on the same
step as the upper note's 6th, 7th, 4th and 7th. `N` is read from the song's
current tuning when the note starts; `Tuning::PERCUSSION` has no steps, so
partials are plain harmonics there.

`stretch` (ε) adds a small, bounded inharmonicity: partial n sits at
`grid(n) + (n-1)·ε` times the fundamental, the same form the FM electric pianos
get from a modulator at 1+ε. The fundamental is never moved, partial n is at
most about `1200·log2(1+ε)` cents above its grid position (1.7 cents at
ε = 0.001), and the partials two notes share differ by at most about ε times the
lower note's fundamental for intervals up to an octave. A stiff string's
`n·sqrt(1+B·n²)` stretch is not used: it would detune exactly the partials
septimal intervals meet at.

## Excitation

A string's modes start at the level its excitation gives them, then the whole
note is scaled to a fixed total power, so every key and velocity has the same
overall level (velocity's loudness comes from the voice gain).

- **Hammer** (`excitation="hammer"`): mode n is struck at fraction `strike` of
  the string, so it starts in proportion to `|sin(n·π·strike)|` (at 1/8, modes
  8, 16, 24 are nulled), times a second-order lowpass at the felt's corner
  `hammerCutoff`. The corner is in Hz, not partial number, so a bass note has
  many partials under it and a treble note few. It scales with the key as
  `(f0 / middle C)^hammerTracking` (0 keeps it fixed in Hz) and with velocity as
  `(velocity / 0.5)^hammerVelocity`: a harder hit shortens the felt's contact
  time, so is brighter. For a felt whose force grows as compression to the power
  p, contact time goes as velocity^-(p-1)/(p+1), so `hammerVelocity` is
  (p-1)/(p+1); the presets use p = 3, 0.5, a round value rather than a measured
  one.
- **Pluck** (`excitation="pluck"`): a displaced triangle, mode n starting as
  `|sin(n·π·strike)|/n²`, with an optional fixed lowpass `pluckCutoff` (a finger
  is softer than a pick). A pluck's brightness does not depend on velocity.
- **Modes** (`modes="1 2.756 5.404 8.933"`): a list of frequency ratios in place
  of the harmonic series, for a bar or bell. The modes sit at the note's
  frequency times the ratio, off the tuning and with no stretch or comb.

`partialFloor` (dB) leaves out modes that start that far below the strongest.

## Decay

`α_n = (decayA + decayB·f_n^decayP) · (f0 / middle C)^decayTracking` nepers per
second, `amplitude(t) = amplitude(0)·exp(-α_n·t)`: higher partials die first, and
a positive `decayTracking` makes lower keys ring longer. With several strings the
outer ones decay `(1 ± decaySpread)` times as fast as the middle one; the sum of
unequal exponentials is a fast first decay followed by a slow aftersound, and
the strings, a cent apart, beat inside it.

## Strings, body and space

`unisonVoices` (1-3) strings per note, `unisonDetune` cents apart (centred, so
the note's pitch is exact; a small hashed jitter keeps the copies from beating at
a mechanical rate). The body is a short list of fixed-frequency resonators, the
same for every key and not tuned to the note, at `thump` times the note's own
level, scaled by `(middle C / f0)^thumpTracking` so a low key knocks harder; the
table is in the preset, not an attribute. Each string and each body
mode is its own output row, placed at its own direction and all encoded in one
pass: `keyboardSpread` degrees of azimuth across the keyboard (bass left, treble
right, over an 88-key span centred on middle C), `stringSpread` degrees between a
key's adjacent strings, and `thumpWidth` degrees over which the body modes are
spread around the track's position, so the body is wider than the strings.

## Attributes

| Attribute | Meaning |
|---|---|
| `preset` | Below. Supplies every other attribute's default; an explicit attribute overrides it. An unknown name is `default`. |
| `partials` | Modes per string (those past Nyquist are skipped). |
| `tuningMatched` | `false` leaves partials at plain harmonics (plus `stretch`). Default `true`. |
| `stretch` | ε above. |
| `unisonVoices` / `unisonDetune` | Strings per note and their spacing in cents. |
| `excitation` | `hammer` or `pluck`. |
| `strike` | Strike or pluck point, fraction of the string. |
| `hammerCutoff` / `hammerTracking` / `hammerVelocity` | The hammer's lowpass corner in Hz at middle C and velocity 0.5, and its exponents against key and velocity. |
| `pluckCutoff` | A pluck's fixed lowpass corner in Hz; 0 is off. |
| `modes` | Mode ratios replacing the harmonic series; empty for a string. |
| `partialFloor` | Modes starting this many dB below the strongest are left out. |
| `decayA` / `decayB` / `decayP` | The decay formula above. |
| `decayTracking` / `decaySpread` | Key tracking of the decay, and the spread of the strings' decay rates. |
| `thump` / `thumpTracking` / `thumpWidth` | The body's level against the note's level, its exponent against the key (bass louder), and its spread in degrees. 0 is off. |
| `keyboardSpread` / `stringSpread` | Degrees of azimuth, above. |
| `level` | Output gain multiplier. Default 1. |

## Engine

Each partial is a coupled-form recursive oscillator (`y[n] = 2·cos(w)·y[n-1] -
y[n-2]`), with its state in flat parallel arrays and eight partials per vector
lane group. A partial above Nyquist is never added; one 90 dB below its own
starting level is culled from its row (checked once per `render()`), since decay
is monotonic. Start phases come from `dsp::HashField` and are the same for a
given `NoteCoordinate` on every platform.

Measured on one machine (Release, 48 kHz, 1024-frame blocks, the voice chain with
the ambisonic encode): a fresh `piano` voice has at most 171 resonators and costs
about 78 µs a block, and 32 simultaneous piano voices about 1.9 ms in total, 9%
of the 21.3 ms block budget. `SYNTH_TIMING=1 SYNTH_TIMING_PRESET=piano
./build/tests/synth_tests` prints the figures. `tools/analyze_render.py` measures
a `--render` WAV: the spectral lines near a frequency (a chord's shared
partials), per-partial level over time, A-weighted level and peak.

## Presets

Only a few numbers come from a source or a derivation: the strike point of 1/8
and 1 cent between unison strings (the brief), `stretch` 0.001 (the FM pianos'),
the felt exponent, and the bar's mode ratios (the roots of `cos(x)·cosh(x) = 1`).
Every other value is a starting value to listen to and adjust, not a measurement
of a real instrument.

| Preset | Character |
|---|---|
| `default` | A plain struck string: one string, partials on the tuning, a mild hammer, moderate decay. |
| `piano` | Three strings 2.5 cents apart, 80 partials on the tuning with `stretch` 0.003, struck at 1/8 by a hammer with a 3 kHz corner that rises with the key, outer strings decaying 70% faster and slower, bass ringing longer, a five-mode body knocking harder on low keys, spread 90°. The library's `piano.acoustic.grand` when the SoundFont has no piano; a SoundFont's own piano wins. |
| `guitar-nylon` | One string plucked at 1/5 with a soft fingertip (1.5 kHz), high partials dying quickly, a small body. |
| `guitar-steel` | The same with a bright pick (5 kHz), a longer ring, more stretch. |
| `harp` | Long decays lengthening strongly toward the bass, strings spread 60° across the keyboard. |
| `harpsichord` | Two plucked strings, a bright fixed pluck, a fast decay with no aftersound. |
| `bar` | An ideal free-free bar: modes 1 : 2.756 : 5.404 : 8.933. Not a marimba, whose bars are cut to put the second mode near 4:1. |

`songs/additive_presets_demo.xml` plays each preset through a 31-EDO scale, a
7:4 dyad and the 4:5:6:7 tetrad; `songs/additive_septimal_chords.xml` plays the
septimal dyads and tetrads on the piano.

## Tuning the presets

The preset values are starting values, not measurements of real instruments. The
first listen found them wrong: every preset sounded much alike, `piano` sounded
like one string, and its thump could not be heard. The numbers behind that, on
the model's own output (`piano`, velocity 0.8), and the first adjustment, made
from numbers rather than by ear:

| | First values | Adjusted |
|---|---|---|
| Body energy against the strings in the first 100 ms, C2 / C4 / C7 | -46 / -34 / -23 dB (whole note) | +6 / -10 / -14 dB, so a knock on the bass and little on the treble |
| `thump`, `thumpTracking` | 0.3 of the *strongest string partial*, 0 | 0.2 of the *note's level*, 0.7 |
| Body modes | 70/120/200 Hz, α 60/50/70 (14-20 ms) | 55/90/140/230/350 Hz, α 35/30/28/35/45 (22-36 ms) |
| `unisonDetune` | 1 cent: 0.15 Hz beat on C4's fundamental, 1.2 Hz on its 8th partial | 2.5 cents: 0.38 / 3 Hz |
| `stretch` | 0.001: partial 10 sharp by 1.6 cents | 0.003: 4.7 cents |
| `decaySpread` | 0.5 | 0.7 |
| `hammerCutoff`, `hammerTracking` | 2000, 0 | 3000, 0.3 |
| `partials` | 64 | 80 |
| Library envelope attack | 5 ms | 2 ms |

The body level was relative to the strongest string partial, which made the thump
weakest on the bass; it is now relative to the note's own level and tracks the key.

The guitars, harp and harpsichord got longer-ringing bodies (3 modes each, α 10-40),
a thump near -8 to -13 dB in the first 100 ms, and decay constants set from rough
expectations of how long each instrument rings (nylon about 6 s, steel longer, harp
longer still, harpsichord about 1.5 s), which are guesses.

Whether these sound right is for the ear. Everything below is a lever, with the
current `piano` value; "try" is an experiment, not a claim about a real piano.

| Parameter | Now | What it does | Try |
|---|---|---|---|
| `thump` | 0.2 | Body level against the note's level. | 0.1 to 0.5. |
| `thumpTracking` | 0.7 | Body exponent against the key; bass knocks harder. | 0.3 to 1. |
| body table (`AdditivePresets.h` only, not an attribute yet) | 55/90/140/230/350 Hz, α 35/30/28/35/45 | Frequency, level and decay of each body mode. | More modes; α 10 to 25 for a longer ring. |
| `thumpWidth` | 90 | Arc the body modes are spread over (degrees). | 60 to 120. |
| `unisonDetune` | 2.5 | Cents between adjacent strings. | 1 to 6. |
| `decaySpread` | 0.7 | Outer strings decay (1 ± spread) times the middle's. | 0.5 to 0.9. |
| `stretch` | 0.003 | Bounded inharmonicity, at most 1200·log2(1+ε) cents. | 0.001 to 0.01. The limit is septimal accuracy: shared partials differ by up to ε times the lower fundamental. |
| `hammerCutoff` | 3000 | Hammer lowpass corner in Hz at middle C, velocity 0.5. | 1500 to 6000. |
| `hammerVelocity` | 0.5 | Corner exponent against velocity (felt exponent 3). | 0.3 to 1. |
| `hammerTracking` | 0.3 | Corner exponent against the key; 0 is fixed in Hz. | 0 to 0.5. |
| `strike` | 0.125 | Strike point; nulls modes 8, 16, 24. | 0.1 to 0.18. |
| `decayA` / `decayB` / `decayP` | 0.05 / 1e-4 / 1.5 | Decay rate a + b·f^p. | Raise B for faster-dying highs; lower A for a longer fundamental. |
| `decayTracking` | 0.5 | Decay exponent against the key; bass rings longer. | 0.3 to 0.8. |
| `partials` | 80 | Modes per string; the bass is cut at 80 × f0. | 64 to 128. |
| `keyboardSpread` / `stringSpread` | 0 / 0 | Stereo placement of keys and strings (degrees). | 40 / 1, as in the demo song. |
| envelope in `piano.acoustic.grand` | 2 ms attack, 8 s decay, sustain 0, 0.3 s release | Wraps the bank. | 1 to 5 ms. |

For the other presets the same table applies, with `pluckCutoff` in place of the
hammer terms. `tools/analyze_render.py` measures a render (spectral lines,
per-partial level, A-weighted level, peak); the energy figures above come from the
model's own partial list.

## Not yet implemented

- No live/automatable control of any attribute.
- Sympathetic resonance of undamped strings, which needs state shared across
  voices.
