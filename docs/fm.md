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
| `detune` | Pitch offset in cents (default 0), for layering detuned unison copies. Each detune starts at its own phase, so copies don't cancel each other. |
| `level` | Output level (default 1). |

`piano.electric.fm` (GM Electric Piano 2) is a `<group>` of `<fm>` layers,
each in its own `<envelope>`: a bright `ratio="1"` pair whose index fades
first, a mellower `ratio="1.001"` pair, a `ratio="2"` pair for the odd
partials and a short strike at `ratio="4.73"`, on carriers a cent or two
apart. Why it is built this way is in the README, under Pianos and Just
Intervals.

Instruments no longer take children: nesting an instrument inside a leaf
instrument (`<oscillator>`, `<fm>`, `<padsynth>`, ...) is a load error.
Instruments don't wrap other instruments; only effects and groups take children.
