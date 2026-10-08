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
same for every key and not tuned to the note, at `thump` times the strongest
partial's level; it is in the preset, not an attribute. Each string and each body
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
| `thump` / `thumpWidth` | The body's level and its spread in degrees. 0 is off. |
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
| `piano` | Three strings a cent apart, 64 partials on the tuning with `stretch` 0.001, struck at 1/8 by a hammer with a fixed 2 kHz corner, outer strings decaying 50% faster and slower, bass ringing longer, a short wide body thump. The library's `piano.acoustic.grand` when the SoundFont has no piano; a SoundFont's own piano wins. |
| `guitar-nylon` | One string plucked at 1/5 with a soft fingertip (1.5 kHz), high partials dying quickly, a small body. |
| `guitar-steel` | The same with a bright pick (5 kHz), a longer ring, more stretch. |
| `harp` | Long decays lengthening strongly toward the bass, strings spread 60° across the keyboard. |
| `harpsichord` | Two plucked strings, a bright fixed pluck, a fast decay with no aftersound. |
| `bar` | An ideal free-free bar: modes 1 : 2.756 : 5.404 : 8.933. Not a marimba, whose bars are cut to put the second mode near 4:1. |

`songs/additive_presets_demo.xml` plays each preset through a 31-EDO scale, a
7:4 dyad and the 4:5:6:7 tetrad; `songs/additive_septimal_chords.xml` plays the
septimal dyads and tetrads on the piano.

## Tuning the presets

The preset values are starting values, not measurements, and the first listen
found them wrong: every preset sounds much alike, `piano` sounds like one
string, and its thump cannot be heard. Measured on the model's own output
(`piano`, velocity 0.8):

- **The thump carries almost no energy.** Its three body modes sit 10.5 dB below
  the strongest string partial and die in 14-20 ms, so their share of the
  note's energy is 0.003% at C2, 0.04% (-34 dB) at C4 and 0.5% (-23 dB) at C6.
- **The three strings behave as one.** A cent between strings beats at 0.15 Hz
  on C4's fundamental (a beat every 6.6 s, longer than the note's first decay)
  and 1.2 Hz on its 8th partial. The bounded stretch moves partial 10 by 1.6
  cents, so the partials are effectively harmonic: no stiff-string shimmer.
- **Presets differ only in a few numbers.** They share one model, and what makes
  a real instrument's body and attack distinctive (many body resonances, the
  attack's own spectrum) is three weak modes here.

What to change, with the current `piano` value. The "try" column is an
experiment to listen to, not a claim about a real piano.

| Parameter | Now | What it does | Try |
|---|---|---|---|
| `thump` | 0.3 | Body level against the strongest partial. | 1 to 3; the body should be heard as a low knock at the onset. |
| body table (`AdditivePresets.h` only) | 70/120/200 Hz, α 60/50/70 | Frequency, level and decay of each body mode; not an attribute yet. | Longer decays (α 10 to 25), more modes, levels near 1. |
| `thumpWidth` | 60 | Arc the body modes are spread over. | 90 to 120 if the thump sounds narrow. |
| `unisonDetune` | 1 | Cents between adjacent strings. | 2 to 6 to hear beating within the first second. |
| `decaySpread` | 0.5 | Outer strings decay (1 ± spread) times the middle's. | 0.5 to 0.9 for a stronger double decay. |
| `stretch` | 0.001 | Bounded inharmonicity, at most 1200·log2(1+ε) cents. | 0.003 to 0.01. The limit is septimal accuracy: shared partials differ by up to ε times the lower fundamental. |
| `hammerCutoff` | 2000 | Hammer lowpass corner in Hz at middle C, velocity 0.5. | 1500 to 6000 for darker or brighter. |
| `hammerVelocity` | 0.5 | Corner exponent against velocity (felt exponent 3). | 0.3 to 1 for how much harder hits brighten. |
| `hammerTracking` | 0 | Corner exponent against the key; 0 is fixed in Hz. | 0.2 to 0.5 if the bass is too bright or the treble too dull. |
| `strike` | 0.125 | Strike point; nulls modes 8, 16, 24. | 0.1 to 0.18 for a different comb. |
| `decayA` / `decayB` / `decayP` | 0.05 / 1e-4 / 1.5 | Decay rate a + b·f^p. | Raise B for faster-dying highs; lower A for a longer fundamental. |
| `decayTracking` | 0.5 | Decay rate exponent against the key; bass rings longer. | 0.3 to 0.8. |
| `partials` | 64 | Modes per string; the bass is cut at 64 × f0. | 64 to 128 if the lowest octave sounds hollow. |
| `keyboardSpread` / `stringSpread` | 0 / 0 | Stereo placement of keys and strings. | 40 / 1 are the demo song's values. |
| envelope in `piano.acoustic.grand` | 5 ms attack, 8 s decay, sustain 0, 0.3 s release | Wraps the bank. | A shorter attack for a harder onset. |

For the other presets the same table applies, with `pluckCutoff` in place of the
hammer terms. Their body tables are the main reason they sound alike, so tune
those first.

## Not yet implemented

- No live/automatable control of any attribute.
- Sympathetic resonance of undamped strings, which needs state shared across
  voices.
