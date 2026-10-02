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
| `level` | Output level (default 1). |

`piano.electric.fm` (GM Electric Piano 2) is built from two `<fm>` layers
under one `<envelope>`: a sine body (`ratio="1"`) and a short, bright tine
layer (`ratio="14"`) for the attack.

Instruments no longer take children: nesting an instrument inside a leaf
instrument (`<oscillator>`, `<fm>`, `<padsynth>`, ...) is a load error.
Only `<multiply>` wraps other instruments.
