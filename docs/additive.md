# `<additive>` - sinusoid-bank additive oscillator

An `Instrument` leaf, like `<oscillator>`/`<padsynth>` - goes inside
`<instruments>`, almost always wrapped in an `<envelope>` (`docs/
effects.md`). A bank of independently-decaying sinusoidal partials is
built fresh at note-on (unlike `<padsynth>`'s shared, cached wavetable -
each note's own decay clock and per-partial starting phases are
genuinely independent) and lives exactly as long as the voice that owns
it.

```xml
<instruments>
  <envelope attack="0.005" decay="8.0" sustain="0.0" release="0.3">
    <additive preset="struck-string"/>
  </envelope>
</instruments>
```

Each partial has its own frequency, amplitude, and an independent
exponential decay rate that *rises* with frequency - the standard
struck/plucked-string spectral model, where high partials die out first
and the fundamental rings on: `α_n = decayA + decayB·f_n^decayP`
(nepers/second), `amplitude(t) = amplitude(0) · exp(-α_n·t)`. This decay
is a *timbral* effect - the note's brightness fading as it ages - not a
substitute for the parent `<envelope>`'s own ADSR, which still governs
overall amplitude the same as for any other instrument leaf.

Sine generation is a coupled-form ("magic circle") recursive oscillator
per partial (`y[n] = 2·cos(w)·y[n-1] - y[n-2]`), not a table lookup or a
per-sample `sinf()` call - zero table/interpolation cost, and its state
lays out as flat parallel arrays alongside the per-partial amplitude
envelope, for one uniform, auto-vectorizable per-sample loop. The
accepted tradeoff is slow numerical drift over very long notes,
untroubling here because a partial's own *amplitude* always decays to
inaudibility (see culling, below) well before drift could become
audible - no periodic renormalization is implemented.

Starting phases (per partial, per unison voice) come from
`dsp::HashField`, deterministic across platforms for a fixed
`NoteCoordinate`.

## Performance

Two rules keep an old, mostly-decayed note cheap even at high polyphony:

- A partial whose frequency (after inharmonicity/tuning-matching) is
  already past Nyquist is never added in the first place.
- A partial whose amplitude has decayed 90dB below its own starting
  level is culled from the active set (checked once per `render()` call,
  not per-sample) - decay is monotonic, so a culled partial never needs
  to reappear.

Measured: a single fresh `struck-string` voice (28 partials × 2 unison
copies) costs roughly 6µs per 1024-frame block, falling further as
partials decay and cull; 32 simultaneously-struck voices together cost
roughly 2ms per block - well under 10% of the ~23ms real-time budget a
1024-frame block at 44.1kHz allows.

## Attributes

| Attribute | Meaning |
|---|---|
| `preset` | Below. Supplies every other attribute's default; an explicit attribute always overrides its preset's value. |
| `partials` | How many harmonics to generate (one past Nyquist is skipped automatically). |
| `tilt` | Spectral tilt, dB/octave - `amplitude_n = 10^(tilt·log2(n)/20)`, so a more negative value darkens the tone. |
| `velocityTilt` | How much velocity brightens the tone on top of `tilt`: actual tilt used is `tilt + velocityTilt·(velocity - 0.5)` (velocity already normalized to [0,1]) - a harder hit makes tilt less negative (brighter), a softer one more negative (darker). |
| `unisonVoices` | 1-3 detuned copies of the whole partial set, for beating/chorus. Default 1 (no unison). |
| `unisonDetune` | Spread across `unisonVoices` copies, in cents (jittered per copy via `HashField`, not perfectly even) - meaningless when `unisonVoices` is 1. |
| `inharmonicity` | Stretched-partial coefficient B, default 0. See below - only affects partials above `partialLimit` when `tuningMatched` is on. |
| `decayA` / `decayB` / `decayP` | The per-partial decay formula's own `a`/`b`/`p` (above). |
| `tuningMatched` | `false` disables tuning-matching entirely (see `docs/padsynth.md`'s own section - the same rule, shared via `SpectralBandProfile.h`). Default `true`. |
| `partialLimit` | How many low partials get tuning-matched, and the boundary `inharmonicity` stretches above. Default 8. |
| `attackNoiseLevel` | Level of a short (~15ms), independently-decaying noise burst standing in for a hammer/pluck transient - 0 (default for the plain `default` preset) is silent/off. |
| `level` | Output gain multiplier, matching `<oscillator>`'s own `level`. Default 1.0. |

## Inharmonicity and the tuning-matched limit

`inharmonicity` (B) has no effect at all within the tuning-matched region
(`n <= partialLimit`, when `tuningMatched` is on) - every partial there
stays exactly on its scale step, the same as B=0. A real stiff string's
own fundamental is physically a hair sharp of its "ideal" pitch under the
plain stretch formula `n·sqrt(1+B·n²)` (this is, physically, where a
piano tuner's octave-stretching practice comes from) - accurate, but an
unwanted per-note detuning side effect once tuning-matching is meant to
pin a note's own audible pitch to the scale, so the matched region is
pinned instead.

Above `partialLimit`, the stretch is a direct cents-space shift
referenced from the limit partial's own snapped cents value:

```
cents(n) = cents_matched(partialLimit) + 866 · B · (n² - partialLimit²)
```

exactly continuous at `n = partialLimit` (the shift is 0 there, matching
the tuning-matched ratio precisely) rather than jumping from a snapped
value to an unrelated stretched one. `866 = 1200/(2·ln 2)` is the
small-angle linearization of `1200·log2(sqrt(1+B·n²))` directly in cents,
close to the plain stiff-string formula for the small B values real
strings actually have, and avoids a `sqrt`/`log` per partial.

When `tuningMatched` is off (or there's no scale to map onto -
`Tuning::PERCUSSION`), B stretches every partial via the plain
`n·sqrt(1+B·n²)` formula with no limit/continuity concern, since nothing
is being snapped to begin with.

`inharmonicity` never implements keyboard-wide octave stretching (the
tuning-practice concept of different notes'/octaves' *assigned* pitches
being spread wider than pure 2:1 across the whole instrument) - it only
reshapes one note's own overtone series relative to its own fundamental.
That would require the `Tuning`/frequency-assignment layer itself
(`src/instruments/Tuning.h`) to stretch differently by register, which
nothing here does.

## Presets

| Preset | Character |
|---|---|
| `default` | A modest, generic electric-piano-ish tone - harmonic (B=0), mild tilt, single voice, moderate decay. Falls back to this for any unrecognized `preset` name. |
| `struck-string` | A real struck string: 28 partials, B=0.0008 with `partialLimit=3` (only the fundamental/2nd/3rd harmonic stay pinned to the scale - see below), decay tuned so the fundamental's time constant is around 2 seconds while a several-kHz partial's is tens of milliseconds, a short attack-noise burst for the hammer/pluck transient, and two unison voices a few cents apart (a piano's own multiple-strings-per-note). |

## Not yet implemented

- No live/automatable control of any attribute - XML-only, read once at
  song load, the same as most effects.
