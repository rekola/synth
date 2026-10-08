# Modal piano: reworking `<additive>`

## Context

`<additive>` is a bank of decaying sinusoids (`SinusoidBank`) with a
hand-set spectrum: a tilt in dB/octave of partial number, one decay formula
for every key, a white-noise burst for the attack. `piano.acoustic.grand`
(`InstrumentLibrary.cpp`, `registerFallbackPath()`) is that bank with
`preset="struck-string"` and `unisonVoices="3"`, and it cannot play the
septimal intervals of the otonal and utonal scales in tune (README, "Pianos
and Just Intervals").

The goal is a synthesized acoustic piano: every audible partial on the
song's tuning, the spectrum and decays coming from a piano model (hammer,
strings, soundboard), keyed to the note. The element stays `<additive>`.
The project is green-field, so attributes and presets change freely, with
no compatibility shims.

Nothing is implemented yet. The user listens after each stage; a render
cannot judge "sounds like a piano", only measure the things below.

## Progress

**All four stages are implemented and wait for listening.** Only
`README.md`'s paragraph, `docs/additive.md`, `docs/glossary.md` and the songs
below describe them; the sections after this one are the plan as written, with
the differences listed here.

What exists: `SinusoidBank` over `PartialSpec`s with one output row per group;
`AdditiveModel.{h,cpp}` (grid-tuned partials with bounded stretch, centred
unison strings, hammer and pluck excitation, strike comb, hammer corner in Hz
with key and velocity exponents, partial floor, mode lists, per-string decay
with key tracking and spread, a body table); `AdditiveVoice` placing each
string and body mode in space; presets `default`, `piano`, `guitar-nylon`,
`guitar-steel`, `harp`, `harpsichord`, `bar`; `tools/analyze_render.py`;
`songs/additive_septimal_chords.xml`, `songs/additive_presets_demo.xml`;
`songs/oscillator_demo.xml` and `songs/songtest20.xml` on the new presets.
`tilt`, `velocityTilt`, `attackNoiseLevel`, `inharmonicity`, `partialLimit`
and the `envelope*` remap attributes are gone.

Measured (Release, 48 kHz, 1024-frame blocks, the voice chain with the ambisonic
encode; `SYNTH_TIMING=1 SYNTH_TIMING_PRESET=... synth_tests`):

| | Before (`struck-string`, 2 strings × 28) | After (`piano`, 3 strings, up to 171 resonators) |
|---|---|---|
| One voice, note-on | 7.9 µs | 21.5 µs |
| One voice, first block | 22.4 µs | 77.7 µs |
| 32 voices, first block, total | 835 µs | 1857 µs |
| 32 voices, block 24, total | 613 µs | 1135 µs |

About 0.45 µs a resonator both times; 32 piano voices are 9% of the 21.3 ms
block budget. (The 6 µs and 2 ms figures in the old docs were not reproducible
and are gone.)

Chord check (31-EDO, C4 + A♯4, 7:4, stage 1, decay and tilt switched off in a
scratch variant so the 7th partial is still audible after 0.5 s): the root's
7th partial is 1840.50 Hz at `stretch` 0 and 1842.00 Hz at 0.001 (model:
1842.07), and the upper note's 4th partial is within 0.2 Hz of it, so one
string gives a single line; with three strings the lines sit at 1840.85,
1842.00 and 1843.15 Hz, ±1 cent. Before, the two partials were 795 Hz and
1372 Hz apart (Finding A).

Piano levels, C1…C7 at velocity 127: peaks −11.2, −8.7, −11.3, −13.6, −13.2,
−14.1, −15.6 dBFS (within 5 dB). A-weighted level of the first 150 ms:
−22.4, −21.5, −24.5, −25.0, −27.2, −30.9, −34.0 dB, falling toward the treble
mostly because the starting treble decay (`decayTracking` 0.5, `decayB` 1e-4)
is fast. That, not the starting level, is the thing to listen to; no level
tracking attribute was added since the starting peaks are even. The
tetrad on the piano peaks at −4 dBFS in the first stage and −10 dBFS now; the
fundamental-heavy presets (harp, bar) clipped at a note power of 1 and sit at
−6 dBFS at 0.5, which is `kNoteRms`.

Differences from the text below:
- The stretch bound is `1200·log2(1+ε(n-1)/T(n))` (up to 3% over
  `1200·log2(1+ε)` in 12-EDO); the tests assert it.
- The `envelope*` remap went in Stage 1, when the model replaced its caller.
- Every note has the same total power, `kNoteRms` = 0.5 root-sum-square before
  the string split, instead of an unnormalised tilt.
- The preset values (hammer corner 2 kHz, felt exponent 3, decay constants,
  the body tables, guitar/harp/harpsichord numbers) are starting values for
  listening. No reference piano was available, so none of the "left unset
  until measured" values was measured; they are marked as starting values in
  `AdditivePresets.h` and `docs/additive.md`. The piano preset caps at 64
  partials, which at the lowest key stops at 2 kHz (the hammer corner).
- Pruning with `partialFloor` rarely engages on the piano (nothing falls 60 dB
  below the strongest under a 2 kHz corner within 64 partials); Nyquist and
  the partial cap bound the count.
- The body modes are in the preset, not an XML attribute.
- Stage 4 did not add per-mode decay multipliers (`modes`' optional second
  list); the frequency-dependent decay covers it.

## What the code does today (checked)

| # | Claim | Finding |
|---|---|---|
| 1 | Septimal chords are out of tune by design | True, and worse than stated. See "Finding A". |
| 2 | `unisonDetune` is 6 cents | True (`AdditivePresets.h`, `kStruckString`). The library piano also sets `unisonVoices=3`, so offsets are -6/0/+6 cents plus up to 15% jitter: the outer strings are about 12 cents apart. |
| 3 | Spectrum is a tilt of partial number | True (`SinusoidBank::buildPartials()`, `10^(tilt·log2(n)/20)`); `velocityTilt` only moves the slope. |
| 4 | No two-stage decay | True: every unison copy shares one decay formula, so copies only beat. |
| 5 | Attack is a ~15 ms noise burst | True (`AdditiveVoice.h`, `kAttackNoiseDurationSec`). |
| 6 | Nothing tracks the key | True: `decayA/B/P` act on partial frequency only, `tilt` on partial number only. |

**Finding A: the stretched partials collapse.** For `inharmonicity > 0` with
tuning-matching on, `additivePartialRatio()` (`SinusoidBank.cpp`) returns
`limit_cents + 866·B·(n² - limit²)`. That starts from the *limit partial's*
cents and omits the `1200·log2(n/limit)` that places partial n above it. Run
against the real function (31-EDO, B = 0.0008, `partialLimit` = 3):

| n | ratio | cents from n·f0 |
|---|---|---|
| 3 | 2.991 | -5.2 |
| 4 | 2.999 | -498 |
| 7 | 3.039 | -1444 |
| 12 | 3.157 | -2312 |
| 28 | 4.079 | -3335 |

Partials 4-28 of `struck-string` all sit between 3.0 and 4.1 times the
fundamental, a cluster rather than a harmonic series, which is probably most
of why it reads as a plucked nylon string (the library comment says so). The
cluster does not coincide between notes either: for C4 + A♯4 (7:4) in
31-EDO the root's partial 7 is at 795 Hz and the upper note's partial 4 at
1372 Hz. The 28/93-cent figures in the brief are the stretch term alone;
the whole formula is replaced in Stage 1, and a regression test (n ≥ n - 0.5
for every n, whatever the stretch) goes in with it. `docs/additive.md`
documents the same wrong formula.

Other facts the design depends on:

- `InstrumentVoice::playNote()` already sets the voice gain to
  `20·log10(velocity)`, so amplitude is linear in velocity before the bank
  runs. The hammer model shapes the *spectrum* with velocity; it must not
  scale level again.
- `Additive` carries `envelopeAnchor`/`envelopeTracking`/`envelopePostprocess*`
  (anchored spectral-envelope remap). Nothing under `songs/`, `tests/` or
  `docs/additive.md` uses them on `<additive>` (only `<padsynth>` does), and
  the Hz-based hammer filter below makes them redundant. They go.
- `docs/additive.md`'s performance figures disagree: "6 µs per 1024-frame
  block" for one fresh voice (28 × 2 = 56 resonators) cannot give "roughly
  2 ms per block" for 32 voices (32 × 6 µs ≈ 0.2 ms). The 6 µs is presumably
  the bank alone and the 2 ms the whole chain (envelope, ambisonic encode,
  floor reflection). Neither is trusted below; Stage 1 re-measures both.
- A bare leaf (`<additive>` without `<envelope>`) plays until note-off, so
  measurement fixtures can run the bank without the parent envelope's decay
  stacking on its own.

## Architecture

`SinusoidBank` keeps the engine: coupled-form resonators, per-partial
exponential decay, Nyquist skip, -90 dB cull, `HashField` start phases,
8-lane `Vec8` loop. It stops computing the spectrum. Today `Params` carries
tilt, inharmonicity, snapping and decay formula, so the model and the
engine are one unit that tests cannot separate.

- `SinusoidBank` takes a `std::vector<PartialSpec>` (`frequency_hz`,
  `amplitude`, `alpha`, `phase`, `group`) and builds its arrays from it,
  grouped so that `render()` fills one output row per group.
  `getActivePartialCountForTest()`/`getPartialAmplitudeForTest()` stay.
- New `src/instruments/AdditiveModel.{h,cpp}` (add to the `synth_engine`
  list in `CMakeLists.txt`): a pure function `buildPartialSpecs(const
  AdditiveModelParams &, const NoteContext &)` returning the specs.
  `NoteContext` is f0, velocity, sample rate, `edo_steps`, `NoteCoordinate`.
  No audio, no allocation after return, so tests check frequencies,
  amplitudes and decays directly.
- `AdditiveVoice::trigger()` calls the model and constructs the bank. The
  noise generator, its salt and `attackNoiseLevel` are deleted (the thump
  arrives as specs, Stage 3).
- Space. A piano is many sources, not one: up to three strings per key
  across a keyboard about as wide as the instrument, and a soundboard that
  radiates over its whole area. So the bank is not summed to one mono
  buffer. `PartialSpec` carries a *group* (one per unison string, one per
  thump mode); `SinusoidBank::render()` writes one row per group (the 8-lane
  loop already sums per lane group; groups are padded to lane multiples
  separately), and `AdditiveVoice` encodes the rows in one pass with
  `AmbisonicStackEncoder`, each row at its own direction, using the
  protected `PositionedVoice` helpers that `OscillatorVoice` already uses
  for its cloud of directions (`position.extent` included). The floor
  reflection and Aux sends run once on the summed signal, as for the
  oscillator array. Rows: strings (1-3) plus thump modes (a handful), so
  the encode cost stays a small multiple of today's single encode.
  Directions (angles are listening-tuned attributes; the sources give no
  numbers for them and none are assumed):
  - *Key position*: the note's azimuth is the track's plus
    `keyboardSpread` × a position in [-0.5, 0.5] that rises with the key
    (bass left, treble right as the pianist sits). Default 0 until heard.
  - *String position*: the strings of one key are centimetres apart, which
    is a fraction of a degree at listening distance, so `stringSpread` is
    small by default and is mostly there so each string reaches the ears
    with its own phase; whether that audibly helps is a listening
    question, with 0 as the A/B.
  - *Thump*: the soundboard is an extended radiator, so each thump mode is
    placed at its own direction across `thumpWidth` degrees (dealt
    alternately left and right, so the sum is centred but wide), wider than
    the strings. `AmbisonicDiffuseEncoder` (decorrelating allpass chains,
    16 per voice) is the alternative; at 32 voices it costs far more than
    a few extra rows, so it is only considered if the dealt modes sound
    narrow.
  Sympathetic resonance (undamped strings ringing along with the note,
  another "many strings" effect) needs state shared across voices, which
  per-note banks cannot hold; it is outside this plan and noted as a
  later, separate piece of work.
- `SpectralEnvelopeRemap.h` stays for `<padsynth>`; its header comment that
  names `SinusoidBank` as a caller is corrected.
- `tests/SinusoidBankTests.cpp` is rewritten to build specs directly (its
  `Params` aggregate goes). The five behaviors it covers (determinism,
  per-partial decay, higher-decays-faster, Nyquist skip, cull) stay.

## Attributes (proposed final set)

Names keep piano terms already in the code: a *unison* is the group of
strings struck together for one key.

| Attribute | Stage | Meaning |
|---|---|---|
| `preset` | all | `default` (a plain struck string: one string, no thump, `stretch` 0, no key tracking) and `piano` (the model below, with the library's string count). Stage 4 adds the test presets `guitar-nylon`, `guitar-steel`, `harp`, `harpsichord` and `bar`. `struck-string` is removed: one string is not a piano, and the name suggested one. An unknown name falls back to `default`. |
| `partials` | 1 | Upper bound on partials built (Nyquist and the audibility floor trim further). |
| `tuningMatched` | 1 | Every built partial sits on the tuning (below). `false` or `Tuning::PERCUSSION`: plain harmonics, and `stretch` still applies. |
| `stretch` | 1 | ε: partial n sits at `T(n) + (n-1)·ε` times f0. Replaces `inharmonicity`, `partialLimit`. |
| `unisonVoices` | 1 | 1-3 strings per note. |
| `unisonDetune` | 1 | Spacing between adjacent strings, cents, centred so the note's pitch is exact (replaces the ±spread meaning). |
| `strike` | 2 | Strike point as a fraction of string length. |
| `hammerCutoff` | 2 | Hammer lowpass corner in Hz at the reference velocity (0.5). |
| `hammerTracking` | 2 | Exponent t: corner scales by `(f0/261.63)^t`. 0 is a pure Hz corner. |
| `hammerVelocity` | 2 | Exponent γ: corner scales by `(velocity/0.5)^γ` (harder = brighter). |
| `partialFloor` | 2 | Partials starting this many dB below the note's strongest are not built. |
| `decayA/B/P` | 3 | Keep: `α = A + B·f^P` for the string's own decay at middle C. |
| `decayTracking` | 3 | Exponent k: all α scale by `(f0/261.63)^k`, so bass rings longer. |
| `decaySpread` | 3 | Spread of α across the unison strings (the double decay). |
| `thump` | 3 | Level of the soundboard thump. 0 is off. |
| `keyboardSpread` | 1 | Degrees of azimuth across the keyboard, bass left to treble right. 0 puts every key at the track's position. |
| `stringSpread` | 1 | Degrees between adjacent strings of one key. |
| `thumpWidth` | 3 | Degrees of arc the thump modes are spread over. |
| `level` | - | Unchanged. |

Removed: `tilt`, `velocityTilt`, `inharmonicity`, `partialLimit`,
`attackNoiseLevel`, `envelope*`. Only the two songs under "Who uses
`<additive>`" still carry any of them (`tilt`, `attackNoiseLevel`); the
loader ignores unknown attributes without a message (a grep for such a
diagnostic finds none).

## Stage 1: tuning model and unison detune

**Model.** `T(n) = 2^(round(E·log2 n)/E)` for every built partial, `E` the
song's steps per octave, from the existing `tuningMatchedPartialRatio()`
called with `partial_limit = partials`. Then
`ratio_n = T(n) + (n-1)·ε`. This is the FM pianos' form (partial n at
`n + (n-1)ε`) laid on the tuning's grid. For two notes at fundamentals
`f_A` and `f_B = r·f_A` (r the tuned ratio, at most an octave) whose
shared partials are `p·k` of A and `q·k` of B, the grid terms cancel
exactly and the difference left is
`ε·f_A·[(pk - 1) - r(qk - 1)] = ε·f_A·[(r - 1) + qk(p/q - r)]`, which is
at most `ε·f_A` while the interval's tuning error `qk(p/q - r)` is small
(the first term alone is `ε·(f_B - f_A)`). Partial 1 is exactly f0: the
fundamental is never stretched.

I checked offline that the grid itself is consistent: for every EDO in
{12, 19, 31, 53} and every interval in {7:6, 8:7, 7:4, 12:7, 3:2, 5:4},
`round(E·log2(p·k)) = round(E·log2(p/q)) + round(E·log2(q·k))` for all
shared partials up to n = 32, so shared partials land on the same step with
no residual. That is an input to the test, not a proof: the test enumerates
and fails loudly on a pair where it does not hold.

What stretch survives: a bounded one. Partial n is
`1200·log2(1+ε(n-1)/T(n))` cents sharp of its grid position, at most about
`1200·log2(1+ε)` (a grid position can sit half a step below n, which
lengthens it by up to 3% in 12-EDO): 1.7 cents at ε = 0.001
(the FM pianos' value, README), 3.5 at 0.002, 5.2 at 0.003. Fletcher's
`n·sqrt(1+B·n²)` is not used: at B = 4·10⁻⁴ it puts partial 12 about 50
cents sharp (the README's 33 cents is that against the other note's
partial 7), which detunes exactly the partials septimal intervals meet at. Default `stretch` = 0.001 for the piano; the listening
step compares 0, 0.001, 0.003. Whether even 0.003 reads as "piano" is a
real question: a few cents is a much weaker version of the inharmonic
shimmer Fletcher's form gives, and it is the cost of the requirement.

**Unison.** Offsets `(i - (S-1)/2)·unisonDetune` for string i of S, plus the
existing `HashField` jitter (reduced from 15% to what keeps the spacing
within 70-130% of nominal), so the mean offset is 0 and pitch stays on the
note. Default `unisonDetune` = 1 cent (brief). The strings keep the existing
per-(string, partial) hashed start phases. Amplitude per string is
`1/S` as today.

**Files.** `SinusoidBank.{h,cpp}` (spec constructor; `additivePartialRatio()`,
`unisonDetuneCentsFor()` move to the model), new `AdditiveModel.{h,cpp}`,
`Additive.{h,cpp}` (parameters, `trigger()` signature), `AdditiveVoice.h`,
`AdditivePresets.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`,
`tests/SinusoidBankTests.cpp`, `tests/AdditiveTests.cpp`, `InstrumentLibrary.cpp`
(drops the `tilt`/`attackNoiseLevel` overrides; keeps `unisonVoices=3`).
In this stage the spectrum is still the tilt, which is **not** deleted
until Stage 2, so each stage can be heard on its own.

**Tests (`tests/AdditiveTests.cpp`).**
- `additive_every_partial_is_on_a_tuning_step`: E ∈ {12,19,31,53}, n =
  1..partials, `stretch = 0`: `ratio_n = 2^(round(E·log2 n)/E)`.
- `additive_partial_n_is_never_far_below_the_harmonic`: n up to 64, all four
  EDOs, `stretch` ∈ {0, 0.001, 0.01}: `ratio_n ≥ n - 0.5`. This is the
  regression for Finding A.
- `additive_stretch_is_bounded`: at most `1200·log2(1+ε)` cents above the
  grid, and partial 1 is exactly 1.
- `additive_septimal_shared_partials_meet`: 4 EDOs × {7:6, 8:7, 7:4, 12:7},
  f0 = 261.63, every shared pair up to n = 24: difference equals the closed
  form above; with `stretch = 0` it is below 1e-3 cents (float `powf`); with
  `stretch = 0.001` it is at most `2·ε·f_A` for shared partials up to n =
  24. (The factor 2 is because 12-EDO's coarse 12:7 and 7:4 push the
  `qk(p/q - r)` term past 1 at k = 2; the closed-form check above is the
  exact one.) A pair that breaks a bound is reported with its numbers
  rather than hidden by a looser tolerance.
- `additive_31edo_partial_3_is_5_2_cents_flat` (`T(3)` is `2^(49/31)`).
- `additive_unison_strings_are_centred_and_about_one_cent_apart`: 2 and 3
  strings, mean offset 0 (within the jitter), adjacent spacing 0.7-1.3 ×
  `unisonDetune`, 1 string exactly 0.
- `additive_tuning_matching_off_gives_plain_harmonics_plus_stretch`.
- Rewritten `sinusoid_bank_*` tests over `PartialSpec`, plus
  `sinusoid_bank_groups_sum_to_the_ungrouped_output` (rows added together
  equal the single-row render, so grouping changes nothing but routing).
- `additive_key_position_rises_with_the_key` (azimuth monotonic from bass to
  treble, centred on the track at key 60, exactly the track's with
  `keyboardSpread = 0`) and `additive_strings_are_placed_symmetrically`
  (mean offset 0, one string exactly at the key's position).
- `additive_library_piano_septimal_chord_renders` (below), finite and
  audible.

**Measure.** All with `./build/synth --stereo --render` so the SOFA/HRTF
decode does not colour levels (the default render uses whichever decoder is
available; the cardioid one is the neutral reference).
1. Chord fixture, 31-EDO (below): per shared partial, the analyzer reports
   each line's frequency and the beat rate between the two notes' lines.
   Before: lines hundreds of Hz apart (Finding A). After, with one string
   and `stretch = 0.001`: C4 + A♯4's partial 7 lines differ by about 0.2 Hz.
   With three strings 1 cent apart the lines spread by about 1 Hz at
   partial 7 (1 cent = 1.06 Hz at 1832 Hz), which is the piano's own
   shimmer and not tuning error, so the pass criterion is "no line pair
   is further apart than the unison spread plus ε·f_A".
2. CPU, before the change: a throwaway timing loop (a `tests/` TEST that
   prints only when `SYNTH_TIMING` is set and asserts nothing about time)
   running one fresh voice's `render(1024)` and 32 together, bank alone and
   through `renderSongOffline()`. This fixes the baseline that
   `docs/additive.md`'s two figures disagree about. The deterministic
   proxy that *is* asserted in tests is the resonator count (below).

**Listen** (`songs/` fixture below plus the user's own): the septimal dyads
and the 4:5:6:7 tetrad should stop beating hard and read as sitting in
tune. A single note will sound brighter and richer than before (the
collapsed cluster is gone), and probably harsher, since the tilt is still
the old one; that is expected and Stage 2 is what shapes it. Also compare
`stretch` 0 / 0.001 / 0.003, and one/two/three strings.

## Stage 2: hammer excitation and key tracking

**Model.** Each string's partial n starts at
`A_n = |sin(n·π·strike)| · H(f_n)`, normalised so the strongest partial of
the note is 1.

- The comb: a string struck at fraction β has mode n excited in proportion
  to the mode shape at the strike point, `sin(nπβ)`. β = 1/8 (brief) nulls
  partials 8, 16, 24. `n` is the *physical mode number*, not the snapped
  frequency.
- `H(f)`: the hammer's force pulse spectrum, taken as a second-order
  lowpass magnitude `1/sqrt(1+(f/f_c)⁴)` in absolute Hz. The shape is a
  modelling choice (a smooth pulse falls about 12 dB/octave); alternatives
  are A/B'd by listening.
- `f_c = hammerCutoff · (f0/261.63)^hammerTracking · (velocity/0.5)^hammerVelocity`.
  Contact time for a felt that pushes back as `F = K·δ^p` scales as
  `v^(-(p-1)/(p+1))` (energy argument: peak compression grows as
  `v^(2/(p+1))`, time as compression over speed), so `f_c ∝ 1/T_c`, and
  `hammerVelocity = (p-1)/(p+1)`. `p` comes from Chaigne & Askenfelt 1994
  before the default is set; if it can't be sourced it is fitted from the
  reference measurement below instead of assumed.
- Because `f_c` is in Hz, a bass note with f0 = 65 Hz has many partials
  under the corner and a treble note only a few, which is what the FM
  pianos' index tracking did (`indexTracking = 1` keeps the bandwidth
  fixed in Hz). `hammerTracking` is the knob if a pure-Hz corner is wrong
  at the extremes.
- `partialFloor`: weak upper partials are skipped at build time, since the
  engine only culls a partial relative to *its own* start. This bounds the
  resonator count at the bass, where the Hz corner and the sub-Nyquist
  range admit the most partials. The threshold is chosen with a null test:
  render a note with and without the pruned partials and require the
  difference to be below the A-weighted level where it stops being audible.
  The exact figure is decided in the stage, not now.
- Level is untouched (already linear in velocity upstream). Body/
  radiation (bass fundamentals radiate weakly) is **not** modelled until
  the measurement below shows a residual of that shape; then it is one
  high-pass or low-order EQ term in Hz, not a table.

`tilt`, `velocityTilt` and the `envelope*` remap are deleted here.

**Files.** `AdditiveModel.{h,cpp}`, `AdditivePresets.h`, `Additive.{h,cpp}`,
`AdditiveVoice.h` (drops velocity→tilt), `SpectralEnvelopeRemap.h` comment,
`InstrumentLibrary.cpp`, tests.

**Tests.**
- `additive_strike_comb_nulls_partial_8_at_one_eighth`
  (`|sin(8π/8)|` ≈ 0 → amplitude below 1e-4 of the maximum), and partial 8
  is alive at `strike = 0.1`.
- `additive_harder_velocity_is_brighter`: the ratio of the amplitude of the
  partial nearest 4 kHz to the fundamental's rises monotonically with
  velocity 0.25 / 0.5 / 1.0.
- `additive_cutoff_is_in_hz`: with `hammerTracking = 0`, partials at equal
  absolute frequency have the same `H` for f0 = 65 and 523 Hz (so the
  bass has more audible partials); with `hammerTracking = 1` the count of
  partials above `-20 dB` is equal across the two notes.
- `additive_partial_floor_prunes_and_bounds_the_count`: for every key
  C1..C7 at velocity 1.0 the built resonator count (strings × partials) is
  at most a budget fixed in the test from the Stage 1 timing baseline.
- `additive_hammer_exponent_matches_contact_time_formula` (`γ = (p-1)/(p+1)`
  for p = 2, 3).

**Measure.** (tool and fixtures below)
1. Per-partial levels over the first 200 ms for C2, C4, C6 at velocity
   32/64/127, against the same notes from a reference piano when one is
   available.
2. A-weighted level per octave, notes at C1…C7 at velocity 127. The
   analyzer weights each *measured partial* with the IEC 61672 A-curve and
   sums power, which for a tonal note equals the weighted band level and
   needs no octave filterbank. The per-octave table is what the FM pianos
   were levelled against; the target here is that no octave is more than a
   few dB from its neighbours unless the reference shows it, and the
   specific tolerance is read off the reference.
3. Peak level at full velocity for the same notes: single notes and the
   4:5:6:7 tetrad. Today's `struck-string` and the new `piano` are compared, and
   the piano stays below clipping with the tetrad plus pedal-like overlap.

**Reference.** The repo has no piano recording, and I will not tune from
memory. If the user's machine has the GM SoundFont (`FluidR3_GM.sf2`,
`fluid-soundfont-gm`; it's not in this container) a render of
`piano.acoustic.grand` *with* the font loaded is a sampled real piano and
can be measured with the same analyzer. Only derived numbers (per-partial
levels, decays, attack spectrum) are recorded in the plan/doc; no sample
data is copied into the repository. With no font, values that depend on
measurement keep a neutral default (thump off, no body EQ) and the user's
ear decides.

**Listen.** Single notes across the keyboard at soft/medium/hard: soft should
be dull, hard bright and "hammery"; the bass should have a rich, not
hollow, upper register; no octave should jump out in level.

## Stage 3: two-stage decay and soundboard thump (wide)

**Decay.** String s of S, partial n at frequency `f_n`:
`α(s,n) = (decayA + decayB·f_n^decayP) · (f0/261.63)^decayTracking ·
(1 + decaySpread·u_s)`, with `u_s` spread symmetrically over [-1, 1]
(0 for one string). Summing three exponentials with unequal rates gives the
fast "prompt" decay followed by the slow aftersound, and the detuned strings
beat inside it (Weinreich 1977 explains this by coupling through the
bridge: the strings' in-phase motion drains fast, the out-of-phase motion
slowly). It costs no extra resonators. If listening says the double decay
is not distinct enough, the escape hatch is one extra *shared* resonator per
partial, at the exact grid frequency with a fast `α`, for the lowest
dozen or so partials only (one resonator more per partial instead of S).
Not built unless needed.

`decayTracking` starts at 0.5, the exponent `<fm indexDecayTracking>` and
`keynumToDecay = 50` already use (about 1.41 times longer an octave down,
documented in `docs/fm.md`), as a precedent rather than a measurement; the
reference measurement (decay time of the fundamental and partial 4 against
key) replaces it. `decayA/B/P` are re-derived from the same measurement;
the current `a = 0.05, b = 1e-4, p = 1.5` are not trusted.

**Thump.** The soundboard responds to the bridge the same way whichever key
excites it, which is the point of commuted synthesis (Smith & Van Duyne,
ICMC 1995): fold the soundboard's response into the excitation. Here: a
small fixed set of decaying resonators at fixed absolute frequencies
(not tuned, not snapped, so they are also not subject to `stretch`) added
as extra `PartialSpec`s at note-on, amplitude `thump` times the strongest
partial, with a decay of tens of milliseconds. Mode frequencies, relative
levels and decays are read off the attack of the reference (below), averaged
over keys; with no reference the table is left empty and the thump is off.
Velocity scaling is automatic (the bank sits behind the voice gain). Each
mode is its own group, placed across `thumpWidth` as described under
Architecture, so the thump is wider than the strings. This removes `NoiseGenerator`, the salt and `attackNoiseLevel` from
`AdditiveVoice.h`.

**Files.** `AdditiveModel.{h,cpp}` (decay, thump), `AdditivePresets.h` (the
thump table), `AdditiveVoice.h` (noise removal), `InstrumentLibrary.cpp` (its
wrapper comment is rewritten; the envelope keeps `sustain 0` and a long
decay so voices end, and the interplay of the bank's decay with the
envelope's 80 dB-over-N-seconds is measured with the bare-leaf fixture, not
guessed), tests, docs.

**Tests.**
- `additive_decay_rises_with_frequency_and_falls_with_key` (bass partial at
  equal Hz decays slower than treble).
- `additive_unison_strings_have_different_decay_rates` (the three αs are
  distinct and ordered; with `decaySpread = 0` equal).
- `additive_two_stage_decay`: render partial 1 of C3 with three strings,
  fit its envelope: the early slope is steeper than the late slope by more
  than a stated factor, and with one string it is a single exponential.
- `additive_thump_is_short_and_fixed_in_hz`: the same thump specs at C2 and
  C6, all decayed below -60 dB by 150 ms, none within 1 Hz of a snapped
  partial's frequency.
- `additive_thump_off_by_default_in_plain_preset`.
- `additive_thump_modes_are_spread_wider_than_the_strings` (the arc the
  thump modes span is `thumpWidth`, centred, and larger than the strings'
  span at default settings).
- Library: `additive_piano_*` tests in `InstrumentLibraryTests.cpp` keep
  passing; add `additive_piano_chord_in_31edo_is_finite_and_audible`.

**Measure.** Per-partial level over 0-3 s for partials 1, 2, 4, 7 at C2/C4/C6
with the bare leaf: the two slopes and their crossover per key; the first
80 ms spectrum below 400 Hz with `thump` on and off (the thump is the
difference); peak at full velocity again, since the thump adds energy.

**Listen.** A held note should have a fast initial drop and a long quiet
tail, with slow beating in the tail; a hard strike should have a short low
"thud" under the onset and no hiss; decays should lengthen audibly toward
the bass.

## Measurement tooling

- `tools/analyze_render.py` (stdlib only: `numpy`/`scipy` are not
  installed). `--render` writes a 32-bit float WAV, which Python's `wave`
  module can't read, so the script parses the RIFF header itself.
  Subcommands: `partials` (windowed Goertzel at given frequencies over time:
  level in dB per partial per 20 ms hop), `aweight` (A-weighted level per
  note, in dB, from measured partial power), `peak`, `lines` (strongest
  spectral line within a window of a frequency, for the chord check). It
  takes the expected partial frequencies from the command line, the model's
  grid values printed by the same tool from `edo_steps`, f0 and stretch.
- `tools/make_piano_fixtures.py` writes the render songs (notes C1..C7 at
  the chosen velocities, one per second, bare `<additive>` and library
  `piano.acoustic.grand` variants; the 31-EDO chords) into the scratch
  directory, not the repository, except the one chord song below.
- Rendering at 48 kHz (the default in `main.cpp`), `--stereo`. The
  analyzer also reports the left/right level difference per note (keys
  C1..C7 should walk from left to right when `keyboardSpread` > 0) and the
  inter-channel correlation of the first 80 ms against the following
  second (the thump should be less correlated, i.e. wider, than the
  strings).

## 31-EDO septimal test chord

`songs/additive_septimal_chords.xml` (a new file; `temperament="31edo"`,
tempo 60, one `piano.acoustic.grand` track; rows listed with 31-EDO names
from `docs/31edo_note_numbers.txt`). Each chord held 4 s, then a rest:

| Chord | Notes | Intervals |
|---|---|---|
| 7:6 | C-4 D♯4 | 7 steps |
| 8:7 | C-4 E𝄫4 | 6 steps |
| 7:4 | C-4 A♯4 | 25 steps |
| 12:7 | C-4 B𝄫4 | 24 steps |
| otonal tetrad 4:5:6:7 | C-4 E-4 G-4 A♯4 | 10, 18, 25 |
| utonal tetrad 1/7:1/6:1/5:1/4 | C-4 D♯4 F♯4 A♯4 | 7, 15, 25 |

A 12-EDO control (C-4 + A♯4 in a `12edo` song; the nearest step is 31 cents
off the harmonic seventh, but the partials still coincide on the grid) is a
second tiny song kept in the scratch directory, not the repository. Pass criterion in the analyzer: for each dyad the
shared-partial lines (root's partial 7/8/7/12, upper's partial 6/7/4/7) are
within `unisonDetune`-scale of each other, never the tens of Hz of today.

## Who uses `<additive>`

- `src/instruments/InstrumentLibrary.cpp:320`: `piano.acoustic.grand`
  (registered over the SoundFont's own piano, as the FM electric pianos are; it was a fallback only until a SoundFont piano was reported winning) and its tests,
  `tests/InstrumentLibraryTests.cpp`, `tests/fixtures/library_additive_piano.xml`.
- `tests/fixtures/additive_note.xml` and `tests/AdditiveTests.cpp`
  (`<additive preset="struck-string"/>` at 12-EDO; renders finite and audible;
  the fixture moves to `preset="piano"`).
- `songs/oscillator_demo.xml` lines 55-57 (`preset="struck-string"`) and 75-76
  (`preset="struck-string" unisonVoices="3" attackNoiseLevel="0.04" tilt="-6"`);
  its rows 64-95 and 168 are the listening material. Edited here: both
  become `preset="piano"` (the first with its comment reworded, since a
  plain string is no longer what it demonstrates), the obsolete attributes
  dropped.
- `songs/songtest20.xml` line 13 (on main: one `<additive>`, the
  "Additive Struck-String" instrument): the user's work in progress, edited
  only at the user's request. It says `preset="default"`, the plain single
  string, so the song does not depend on `struck-string`, which the rework
  removes. It will sound different: the partial cluster at 3-4 × f0 is
  gone, replaced by a real series on the 31-EDO grid, and a single string
  has no unison beating. Septimal chords in that song stop beating.
- `docs/additive.md`: full rewrite (model, attributes, tuning rule,
  measured costs, presets); no "used to"/"no longer" wording. Plus the
  `README.md` section "Pianos and Just Intervals": add the acoustic piano
  as the second, grid-based answer next to the FM electric pianos.
  `docs/effects.md` and `docs/padsynth.md` only mention `<additive>` in
  passing and need no change beyond what `docs/glossary.md` adds:
  **Unison** (the strings struck together for one key), **Strike point**
  (hammer position as a fraction of the string), **Aftersound** (the slow
  tail after a piano note's fast initial decay).

## CPU at 32-voice polyphony

Cost is linear in resonators. Today: 28 partials × 2-3 strings (the
library uses 3: 84) per fresh voice, `docs/additive.md` quoting 6 µs per
block for 56. After:

- Resonators per fresh voice = strings × built partials + thump modes
  (about a handful, tens of ms). Built partials are bounded by the
  `partialFloor` and Nyquist, and by `partials`; a mid-keyboard note is
  expected to need 30-60 partials with the Hz corner, a bass note more.
  At 3 strings that is 90-180 resonators, roughly 1-2 times today's
  library piano, **to be measured, not assumed**.
- No change to the loop: the 8-lane kernel, the -90 dB cull (which also
  retires fast-decaying upper partials within the first second) and
  Nyquist skip stay. Build time per note-on rises by the model (one `powf`
  per partial per string at note-on and the `HashField` draws), which is
  off the render path but is on the note-on path; Stage 1's timing run
  includes note-on construction for 32 simultaneous notes (a full chord
  cluster), since that is where a regression would show.
- Acceptance: resonator count at velocity 1.0 for every key C1..C7 is
  asserted in a test (deterministic); the timing run must show 32 voices
  through `renderSongOffline()` at a small fraction of the block budget
  (1024 frames at 48 kHz is 21.3 ms), compared with the baseline taken
  before Stage 1. The doc's two inconsistent figures are replaced by the
  re-measured pair (bank alone, whole chain), with the method written down.

## Does `<padsynth>`'s shared tuning code change?

No change to shared code. `tuningMatchedPartialRatio()` already takes a
partial limit; `<additive>` calls it with `partial_limit = partials`, which
matches every audible partial. `<padsynth>` uses the other function,
`tuningMatchedPartialPosition()`, which matches by the nearest harmonic's
odd part (≤ 7); it is not touched. The two rules differ on purpose-ish
(`SpectralBandProfile.h` calls the additive one a separate feature):
padsynth leaves partials 11, 13, 17… off the grid and additive snaps them.
For the septimal intervals above that is irrelevant, because every
interval involved has odd parts ≤ 7. If listening says snapping 11/13
sounds worse than leaving them, `<additive>` can adopt the odd-part rule
(one call-site change in the model). `docs/padsynth.md`'s "Tuning-matched
partials" paragraph says `<additive>` shares the rule; that sentence is
reworded when `docs/additive.md` is rewritten. One shared-code edit is
comment-only: `SpectralBandProfile.h`'s note that
`tuningMatchedPartialRatio()` backs "SinusoidBank.cpp's own
additive-oscillator inharmonicity model" is rewritten to describe the new
use.

## Sources (and what is deliberately left unset)

- Fletcher, "Normal vibration frequencies of a stiff piano string", JASA
  1964: `f_n = n·f0·sqrt(1+B·n²)`. Used only to size what the bounded
  stretch avoids.
- Weinreich, "Coupled piano strings", JASA 1977: double decay and
  aftersound from string coupling through the bridge.
- Chaigne & Askenfelt, "Numerical simulations of piano strings", JASA 1994:
  the hammer felt force law (the exponent p) and contact time; strike point
  by register. Both numbers to be read from the paper before use.
- Bank, Avanzini, Borin, De Poli, Fontana, Rocchesso, "Physically informed
  signal processing methods for piano sound synthesis", EURASIP 2003:
  the structure of a resonator-per-partial model with hammer, double
  decay and soundboard terms.
- Smith & Van Duyne, "Commuted piano synthesis", ICMC 1995: the soundboard
  response folded into the excitation, reused here as a fixed set of
  resonators.
- Sethares, *Tuning, Timbre, Spectrum, Scale*: partials on the tuning's
  steps.

From the brief: ε = 0.001 (FM precedent, README), β = 1/8, about 1 cent
between unison strings. Derived here: the closed-form shared-partial
difference, the contact-time exponent `(p-1)/(p+1)`, the comb `sin(nπβ)`,
the grid consistency check. Left unset until measured or sourced:
`hammerCutoff`, `hammerVelocity` (p), `hammerTracking`, `partialFloor`,
`decayA/B/P`, `decayTracking`, `decaySpread`, the thump table and level,
register-dependent string counts and strike point, and any body EQ. Each
stage sets them from the reference measurement or, with none, from the
user's ear and records which in `docs/additive.md`.

## Other instruments

The model is a struck or plucked string with a body, and `piano` is one
parameter set over it. Stage 4 (below) adds four more presets, chosen
because each exercises a different code path, not because their sound is
tuned: they are for testing the model and for comparing against the piano.
None is registered as a library instrument path.

What carries over from the piano unchanged: tuning-matched partials,
strike or pluck position (the comb), several strings per note, per-string
decay with key tracking, fixed-frequency body resonators (the thump table
is a body-mode table, so a guitar body is a different table), and placement
of strings and body in space.

Not covered: anything with continuous excitation (bowed strings, winds,
voices). A resonator bank with no driver can only ring down; `<padsynth>`
and `<oscillator>` are the tools there. Electric pianos stay with `<fm>`.

## Stage 4: test presets (guitar, harp, harpsichord, bar)

Done after the piano is heard and settled, since it adds two things to the
model:

- **`excitation`** (`hammer` | `pluck`). A pluck releases the string from a
  displaced shape, so mode n starts roughly as `sin(nπβ)/n²` with β the
  pluck position, and there is no hammer lowpass or velocity-dependent
  corner; a pluck has its own optional fixed lowpass, `pluckCutoff` (Hz: a
  finger is softer than a pick, the main difference between the nylon and
  steel guitar presets). The `hammer*` attributes keep their prefix because they do not
  apply to a pluck; `strike` is the pluck position for both.
- **`modes`**, an explicit list of mode frequency ratios (and optionally a
  decay multiplier each) replacing the string's harmonic series. Tuning
  matching applies to the fundamental only, then each ratio multiplies it.

| Preset | Excitation | Strings | What it exercises |
|---|---|---|---|
| `guitar-nylon` | pluck | 1 | The pluck spectrum and comb at a pluck position that is not the piano's (swept by ear, e.g. 0.1 / 0.2 / 0.3, none assumed); a soft, fingertip pluck (low `pluckCutoff`); high partials dying quickly; a body table of fixed-frequency resonators that is not the piano's thump table; no keyboard spread (one instrument, six strings, one position). |
| `guitar-steel` | pluck | 1 | The same machinery with the differences that make it steel: a brighter pluck (high `pluckCutoff`, a pick or nail), slower decay of the high partials and a longer ring, and a slightly larger `stretch` (steel strings are stiffer, though the bounded form keeps it to a few cents). Its body table differs from the nylon one. This pair checks that a handful of attributes is enough to tell two members of one family apart. |
| `harp` | pluck | 1 | Long decays with strong key tracking over a wide range; `keyboardSpread` and `stringSpread` at larger values than the piano, since a harp's strings really are spread across its width; a small body table. |
| `harpsichord` | pluck | 2 | Two unison strings with a pluck; velocity changes loudness (upstream, linear) but not brightness, which `hammerVelocity`'s absence for a pluck gives for free; a fast decay with no aftersound (`decaySpread` 0). |
| `bar` | hammer | 1 | `modes` with the ideal free-free bar ratios 1 : 2.756 : 5.404 : 8.933 (derived from the roots of `cos(x)·cosh(x) = 1`, 4.730, 7.853, 10.996, 14.137, squared and divided by the first). This is an ideal bar, not a marimba, whose bars are cut to put the second mode near 4:1; that comparison is the first check of the mode list. |

Numbers: the bar ratios above are derived and sourceable. Every other
value in these presets (pluck position, body-mode frequencies and decays,
spreads, decay constants, string counts) is a test value chosen by listening
or read from a reference measurement, and `AdditivePresets.h` marks each
one that is not sourced with a comment saying so. Nothing in the docs
presents them as realistic.

**Files.** `AdditiveModel.{h,cpp}` (excitation, modes), `Additive.{h,cpp}`
(`excitation`, `modes` parameters; `modes` stored as a space-separated
list), `AdditivePresets.h`, tests, `docs/additive.md`, and a listening song
`songs/additive_presets_demo.xml` (31-EDO, one track per preset: a scale, a
dyad and the 4:5:6:7 tetrad each, so every preset is checked in tune with
the same chord set as the piano).

**Tests.**
- `additive_pluck_spectrum_follows_one_over_n_squared_with_comb`: with
  `strike` = 1/5, mode 5 is nulled and, away from nulls, amplitude times
  `n²` is constant.
- `additive_pluck_has_no_velocity_brightening` (the partial amplitude ratios
  are identical at velocity 0.25 and 1.0).
- `additive_modes_follow_the_list_and_only_the_fundamental_is_snapped`: the
  bar preset's mode k sits at the snapped fundamental times the list ratio,
  in 12- and 31-EDO.
- `additive_bar_ratios_match_the_free_free_roots` (2.756, 5.404, 8.933 to
  three decimals from the roots).
- `additive_every_preset_renders_finite_and_audible` for all presets.
- Preset-specific structure: harpsichord builds 2 groups, others 1 plus
  their body modes.

**Measure and listen.** Same analyzer and chord set as the piano; the
septimal chord check passes for every pitched preset (the bar's upper modes
are not on the tuning, by design, so its check is the fundamental only). By
ear: do guitar and harp read as plucked rather than struck, is the bar
metallic in the way an ideal bar should be, and does the harpsichord's
velocity-independent brightness sound right.

## Order of work and risks

1. Stage 1, then stop for listening. Commit nothing (the user tests by
   ear first). The tilt stays through this stage.
2. Stage 2 after the user's go-ahead; the piano preset leaves its tilt
   behind here.
3. Stage 3 likewise.
4. Stage 4 (test presets) after the piano is accepted.
5. Docs rewrite and README/glossary last, once the attribute set has
   settled.

Risks. The bounded stretch is only ~2 cents, so the piano may sound too
clean; the fallback is a larger ε in the low partials only, still bounded.
Snapping to a coarse grid (12-EDO) puts partial 7 about 31 cents sharp and
11 about 49: right for chords in that tuning, odd for a single note, and
the user decides by ear (`tuningMatched="false"` is the A/B). Without a
reference recording, sound quality rests on listening alone; the analyzer
checks the numbers the model promises, not realism.
