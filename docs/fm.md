# `<fm>` - two-operator FM

An `Instrument` leaf: a sine carrier at the note's pitch, phase-modulated by
a sine modulator at `ratio` times that pitch. The pitch always comes from the
carrier, so notes stay on the song's tuning; an integer `ratio` puts the
sidebands on the carrier's harmonic series. Wrap it in an `<envelope>` for
amplitude shaping, like any other instrument.

| Attribute | Meaning |
|---|---|
| `ratio` | Modulator frequency / carrier frequency (default 1). |
| `index` | Peak phase deviation in radians (default 1), scaled by note velocity, so harder notes are brighter. `0` gives a plain sine. |
| `indexDecay` | Time constant in seconds of an exponential decay of the index, giving the bright-then-mellow attack of a struck tine. `0` (default) keeps the index constant. |
| `indexTracking` | Exponent k scaling the index by (261.63 Hz / note frequency)^k (default 0, the same index on every note). `1` keeps the frequency deviation, and so the spectrum's bandwidth, fixed in Hz, so low notes get more partials and stay as loud as high ones - the keyboard level scaling FM synthesizers apply to their modulators. |
| `indexDecayTracking` | Exponent k scaling `indexDecay` by (261.63 Hz / note frequency)^k (default 0). `1` makes the brightness last twice as long an octave down. |
| `feedback` | Phase deviation in radians the modulator applies to itself, from the mean of its last two outputs (default 0). Around 3 turns its sine into a near-sawtooth, for a brighter, buzzier tone. |
| `detune` | Pitch offset in cents (default 0), for layering detuned unison copies. Each detune starts at its own phase, so copies don't cancel each other. |
| `level` | Output level (default 1). |

The library's electric pianos, and a few other GM instruments, are built
from `<fm>` layers after the Yamaha DX7's factory voices, in place of the
SoundFont's sampled ones: each DX7 carrier and its modulators become
parallel `<fm>` pairs, each in its own `<envelope>`, with `indexTracking="1"`,
`indexDecayTracking="0.5"` and `keynumToDecay="50"` so bass notes are as
bright and loud as high ones and ring longer. A DX7 carrier off the note's
pitch becomes a `detune` in cents, and where a voice's lowest carrier sits
below the key, everything is scaled so that it plays the written note.

| Path (GM program) | DX7 voice | Layers |
|---|---|---|
| `piano.electric.tine` (Electric Piano 1) | E.PIANO 3 (ROM 1B) | Mellow: `ratio="2"` and soft `ratio="1.001"` pairs, a `ratio="3"` pair with `feedback` whose index fades within a fraction of a second, a barely audible `ratio="14"` tine. |
| `piano.electric.fm` (Electric Piano 2) | E.PIANO 2 (ROM 1B) | Glassy: a long `ratio="1.002"` body with `feedback`, a brighter `ratio="1"` pair that fades faster, a clangy `ratio="0.51"` strike and an `ratio="11"` tine. |
| `piano.electric.grand` (Electric Grand Piano) | E.GRAND 2 (ROM 3B) | Bright attack settling into a plain tone: two 1:1 pairs, a `ratio="3"` pair with `feedback` and a `ratio="5"` pair. |
| `pad.metallic` (Pad 6, metallic) | T.BL-EXPA (ROM 2B) | Two `ratio="3.5"` tubular-bell strikes over a `ratio="1.002"` pad with `feedback` that swells in over 1.5 s and holds. |
| `texture.crystal` (FX 3, crystal) | SHIMMER (ROM 2B) | A held `ratio="0.995"` tone, a held `ratio="15.71"` sparkle and a decaying carrier a fifth up with `feedback`. |
| `percussion.pitched.metal.tinkle-bell` (Tinkle Bell) | BELLS (ROM 2A) | A sine at the written note and two carriers 2.36 times higher at `ratio="2.669"`: a bell's inharmonic partials. |

Why they are built this way is in the README, under Pianos and Just Intervals.

Only effects and groups take children: nesting anything inside a leaf
instrument (`<oscillator>`, `<fm>`, `<padsynth>`, ...) is a load error.
