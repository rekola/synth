# `<padsynth>` - PADsynth resynthesis oscillator

An `Instrument` leaf, like `<oscillator>` - it goes inside `<instruments>`,
almost always wrapped in an `<envelope>` for amplitude shaping (see
`docs/effects.md`'s "two ways to attach" and its own `<envelope>` row) and
optionally a per-voice `<biquadFilter>`/`<resonantFilter>` (also
`docs/effects.md`). It has no track/voice attachment choice of its own the
way an `Effect` does - a `<padsynth>` is always the thing being played, never
something wrapping another instrument.

```xml
<instruments>
  <envelope attack="0.6" decay="0.4" sustain="0.85" release="1.0">
    <padsynth preset="strings"/>
  </envelope>
</instruments>
```

Every preset reproduces the real spectrum of a specific instrument patch:
a small named oscillator shape (see "Oscillator shape" below) is run
through Paul Nasca's PADsynth algorithm - a harmonic's energy is spread
across a narrow Gaussian-shaped band of frequency bins rather than a
single sharp line, and every bin in the resulting spectrum gets its own
random phase before a single inverse FFT resynthesizes the whole thing
into one long, seamlessly-looping wavetable. The result is a rich,
detuned-sounding pad tone with no audible periodicity, built from purely
harmonic material. One wavetable is built per pitch region and shared by
every voice/note played through that `<padsynth>` node - a real but
one-time cost (see "Performance" below), not something repeated per note.

The numeric parameters behind each preset come from Paul Nasca's own
[PADsynth algorithm description](https://zynaddsubfx.sourceforge.io/doc/PADsynth/PADsynth.htm),
which presents its worked examples in ZynAddSubFX's own patch-parameter
format (that free/open-source synthesizer's own implementation of the
algorithm) - the format the numbers happen to be written in, not their
source. The oscillator-shaping and PADsynth-rendering here were built from
that page's own algorithm description, never any application source.

## Tuning-matched partials

Every oscillator that generates its own harmonic content (`<padsynth>`,
`<additive>`) shares one rule: a partial can be snapped to the nearest
step of the song's own tuning instead of sitting at its plain harmonic
ratio, so it doesn't beat against the tuning's own notes. A partial is
tuning-matched when the nearest integer harmonic's odd part (that
harmonic number with every factor of 2 removed) is `<= 7` - so every
octave-doubling of a small odd number gets matched (1-8, 10, 12, 14, 16,
20, 24, 28, 32, ...), not just a fixed initial run of low partials. `N`
(steps per octave) is read from the *song's own current tuning* at play
time, not a hardcoded constant - a song's temperament change is reflected
the next time the instrument's table is (re)built. `Tuning::PERCUSSION`
has no scale to snap to, so tuning-matching is always a no-op there.

Resampling a table built for one pitch region to a different pitch within
that region keeps every snapped partial locked to its own scale step: the
snap happens once, at generation time, in cents relative to the table's
own fixed reference fundamental, and resampling shifts every partial
(snapped or not) by the same multiplicative ratio - it doesn't recompute
or perturb the snap.

## Attributes

| Attribute | Meaning |
|---|---|
| `preset` | Below. Supplies every other attribute's default; an explicit attribute always overrides its preset's value. |
| `tuningMatched` | `false` disables tuning-matching entirely - every partial stays purely harmonic, for A/B comparison. Defaults to the preset's own value (`true` unless a preset overrides it - e.g. `keyboard`/`synth-piano-3-b` default `false`, to keep their own stretched partials intact). |
| `level` | Output gain multiplier, matching `<oscillator>`'s own `level`. Default 1.0. |
| `seed` | Integer seed for the table's random phases (`dsp/HashField.h`) - deterministic across platforms for a fixed seed; change it to get a different (still fully deterministic) phase draw for the same preset. Default 1. |
| `envelopeAnchor` | Anchor frequency in Hz for the anchored spectral-envelope resampler (below). `<= 0` (the default) means off. |
| `envelopeTracking` | Tracking exponent `p`, `[0, 2]`. `0` is identity; `1` pins the harmonic envelope to a fixed Hz position regardless of the note played; values between 0 and 1 give partial tracking, above 1 overcompensates. |
| `envelopePostprocess` | `"residue"` or `"stretch"` - the postprocess stage (below). Absent means off; independent of `envelopeAnchor`/`envelopeTracking`. |
| `envelopePostprocessN`, `envelopePostprocessR`, `envelopePostprocessAmount` | The selected postprocess primitive's own integer/integer/`[0,1]` parameters. |

## Anchored spectral-envelope resampling

A plain harmonic oscillator gives harmonic `h` the same amplitude at
every pitch, so its spectral envelope scales with the played note's own
fundamental - the "chipmunk effect" when transposed. This feature
resamples the harmonic-amplitude profile along the harmonic-number axis
(`dsp/SpectralEnvelopeRemap.h`) so the envelope instead stays pinned to
fixed absolute frequencies, the way a real formant or a body resonance
does - the note still contains only integer harmonics of its own
fundamental, only each harmonic's *amplitude* is resampled.

With `r = (f / envelopeAnchor)^envelopeTracking` (`f` is the table's own
region frequency): `r <= 1` reads the prototype envelope at a compressed/
expanded fractional position (gather); `r > 1` distributes each prototype
harmonic's own amplitude across its two neighboring output harmonics,
several prototype harmonics converging on one output harmonic where they
overlap (scatter) - accumulated in power (`sqrt(sum of squares)`), correct
for independent-random-phase partials. Runs after the oscillator shape's
own shaping stages and before partial positioning/tuning-matching.

An optional, independent postprocess stage shapes the result further:
`"residue"` (residue-class weighting) keeps harmonics `h` with `h mod n
== r mod n` at full amplitude and scales every other harmonic by
`(1 - amount)`; `"stretch"` (stretch-mix) mixes the spectrum with a copy
of itself stretched by a factor of `n` along the harmonic axis
(`out = (1-amount)*S + amount*S'`, `S'[n*h] = S[h]`).

Partial positioning (`g(h)` - where a preset's own partials actually sit,
e.g. an explicit non-integer sequence or a fractional stiff-string-style
stretch) is preset-only, not an XML attribute, since it's curated per
preset rather than something a song would author directly.

## Presets

| Preset | Character |
|---|---|
| `keyboard` | A power-ramp oscillator shape, an arctangent waveshaper, an oscillator time warp, and fractional-stretch partial positions (partial 10 lands slightly sharp, at 10.04) - tuning matching is off by default so that stretch survives. |
| `synth-piano-3-b` | A sibling of `keyboard`: a Gaussian-pulse oscillator shape, an arctangent-plus-single-harmonic-boost filter chain (the same shaping family as `bells-3`), a slightly stronger partial-10 stretch (10.06), and its own stretch-mix postprocess. |
| `saw-piano` | A power-ramp oscillator shape against 4 harmonics (1/2/4/16). Sustains at full level rather than decaying (not percussive like `keyboard`). |
| `saw-piano-wide` | A wider-bandwidth sibling of `saw-piano`, the same oscillator shape against harmonics 1/2/4. |
| `soft-pad` | A much steeper power-ramp oscillator shape against just harmonic 1, reshaped by a spectrum-adjustment stage - genuinely a single reshaped partial. Used at `pad.newAge`. |
| `strings` | A power-ramp oscillator shape against 2 harmonics. A simple base tone on its own - real ensemble motion comes from `<multiply>` unison layered on top (`pad.bowed`/`string.synth.slow`), the same reasoning `pad.choir`'s own unison uses. Also the generic "plain pad" tone (`pad.warm`, `pad.sweep`) and the base of the Mellotron (`keyboard.tape.mellotron` - a Mellotron "strings" tape *is* a recording of a bowed string ensemble, the same real-world instrument family, so its own tape-machine wow/flutter/hiss/attack-swoop, `<tapeDegradation preset="mellotron">`, is what tells it apart from a live strings pad, not a different spectrum). |
| `dual-strings` | The same power-ramp oscillator shape as `strings`, against just harmonic 1 and its own wider bandwidth. Used at `pad.poly`. |
| `church-organ` | A clipped-triangle oscillator shape against 8 harmonics, shaped by an exponential-lowpass filter - a drawbar-organ-style spectrum. Used as a fallback at `organ.pipe`. |
| `bells` | 3 harmonics placed directly, then reshaped nonlinearly by a logistic-sigmoid waveshaper, with partials landing at 1,2,4,5,7,9,11,13,... rather than a plain harmonic series. The preset's own default is tuning-matched on; used with `tuningMatched="false"` in some songs for a deliberately dissonant bell character (`pad.metallic`). |
| `bells-3` | A Gaussian-pulse oscillator shape against harmonic 1, reshaped by an arctangent waveshaper and a single-harmonic boost filter - the same shaping family as `synth-piano-3-b`. Partials land at 1,3,5,8,12,16,21,27,... |
| `choir-pad4` | A warped-half-sine oscillator shape with its own time warp, an exponential-lowpass filter, and a harmonic shift of 7, plus its own anchored-spectral-envelope-remap settings. Used at `pad.choir`/`lead.voice`. |
| `long-spacechoir2` | The same warped-half-sine/warp/lowpass/shift-of-7 family as `choir-pad4`, with its own warp constants and a spectrum-adjustment stage. Used at `pad.halo`, wrapped in `<phaser>` for its own slow, shimmering motion. |

An unrecognized `preset` name falls back to `strings`.

Two presets outside this list stand in for a character no real patch
covers: `brass.synth`/`brass.synth.soft` use `saw-piano-wide`/`saw-piano`
(a power-ramp oscillator shape reads as a bright, sawtooth-like tone, the
closest real character to a synth-brass section) rather than a preset
built to represent brass specifically.

## Oscillator shape

Every preset is built from a real instrument's own oscillator shape: a
small named base waveform (clipped triangle, power ramp, Gaussian pulse,
or warped half-sine), optionally run through a time warp, then harmonic
expansion against an explicit list of partials, then optionally reshaped
by a waveshaper, a harmonic filter, a second time warp, spectrum
adjustment, and/or a harmonic shift - before the result feeds into
PADsynth's own partial profile/placement stage (a windowed profile placed
per partial, its window width and placement chosen so a partial's total
energy stays the same regardless of how wide its band is) at that
patch's own base note, octave span, and sample layout.

This lets even a preset with a sparse explicit harmonic list (`bells`,
`bells-3`) reproduce its own real, much richer spectrum - the sparse list
is only the starting point the waveshaper/filter then reshapes, not the
whole story.

## Known limitations

No preset here authors per-harmonic *phase* - phase is always drawn
randomly per bin (see the algorithm summary above). Filter envelopes
(a filter's own cutoff frequency changing shape over a note's attack/
decay/release) also aren't modeled: `<biquadFilter>` has an `envelope_`
member but never actually reads it to modulate `fc` today, so a preset
whose real instrument has one (`church-organ`, for instance) doesn't
carry that motion over.

A single `<padsynth>` table also can't produce genuine ensemble motion on
its own, no matter how narrow or wide its bands are: real beating needs
several truly independent voices that each drift to a slightly different
pitch over time, not one voice with wider partials. This isn't a padsynth
feature gap so much as a wrong layer to fix it at - `<multiply
unisons="3" detune="16" spread="0.5">` (`NoteMultiplier.h`/`.cpp` - a
generic, pre-existing per-instrument unison/detune/spread wrapper usable
around any child instrument, not padsynth-specific, and not yet written
up in its own doc page) already solves it by wrapping any child
instrument in several independently-detuned copies. `pad.choir`/
`pad.poly`/`pad.bowed`/`string.synth.slow` (`InstrumentLibrary.cpp`)
are wrapped this way; a plain `<padsynth>` used directly (as most of
`songs/oscillator_demo.xml`'s own comparison tracks deliberately are, to
isolate the preset itself) is not, and reads noticeably thinner/more
static as a result - wrap it in `<multiply>` the same way if genuine
ensemble motion matters more than isolating the raw preset.

## Performance

Table generation costs one inverse FFT per sample point at the preset's
own table length (2^17-2^18 samples) - real but one-time work, amortized
across every note/voice that plays through the same `<padsynth>` node
afterward (cached re-fetch of an already-built table is sub-microsecond).
Per-voice `render()` reads from the cached table with interpolation and
costs on the order of 1000x real time - negligible next to the one-time
table build.

## Not yet implemented

- No live/automatable control of any attribute - everything here is
  XML-only, read once at song load, the same as most effects
  (`docs/effects.md`/`docs/tape_degradation.md`).
- No time-varying motion of any kind (a filter sweep, an evolving
  spectrum) - a `<padsynth>` table is fixed once generated; see the
  instrument library's `pad.sweep` entry for where this gap shows up in
  practice (approximated with a static filtered pad instead).
