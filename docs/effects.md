# Per-track and per-voice effects

Unlike the shared send bus (`docs/bus_effects.md` - one always-on, song-
wide reverb/delay/granular/saturator bed everything can send into), the
effects on this page each attach to *one specific* track or instrument,
processing only what passes through that one spot.

## Two ways to attach the same element

Every effect element below can be written in either of two places in a
song file, and means something different depending on which:

**Wrapping a `<track>`, under `<tracks>`** - runs continuously for as
long as the song plays, regardless of whether that track currently has
any note sounding:

```xml
<tracks>
  <chorus rate="0.3" depth="6">
    <track id="0" instrument="0"/>
  </chorus>
</tracks>
```

**Wrapping an instrument, inside `<instruments>`** - built fresh for
every note and torn down with it, so its own internal state (an
envelope, a delay line, ...) belongs to that one note alone, not shared
across a held chord:

```xml
<instruments>
  <chorus rate="0.3" depth="6">
    <envelope attack="0.01" hold="0.2" decay="0.3" sustain="0.4" release="0.3">
      <oscillator type="saw"/>
    </envelope>
  </chorus>
</instruments>
```

Both examples use identical attributes on identical elements - only
*where* the wrapping happens changes which mode it's in. A given song
can freely mix the two: some tracks wrapped once at the track level,
other instruments wrapped per-note inside `<instruments>`.

Nesting works too, and combines: `<resonantFilter><distortion><track
.../></distortion></resonantFilter>` runs the distorted signal through a
resonant filter, both persistent for that one track.

## What effects exist

| Element | What it does |
|---|---|
| `<chorus>` | Multi-voice, LFO-modulated delay-line chorus. `voices`, `rate` (Hz), `delay`/`depth` (ms, center/modulation range), `mix`. |
| `<distortion>` | Waveshaping distortion. `type` (`hardclip`/`softclip`/`tanh`/`bitcrush`), `drive` (pre-gain before the nonlinearity, all types), `param` - meaning depends on `type`: clip threshold for `hardclip`, bit depth (1-24, default 8) for `bitcrush`, unused for `softclip`/`tanh`. |
| `<compressor>` | Dynamics compressor. `pregain`/`postgain` (dB), `threshold` (dB), `knee` (dB), `ratio`. |
| `<envelope>` | ADSR amplitude envelope. `attack`/`hold`/`decay`/`release` (seconds), `sustain` (0.0-1.0 level). The building block almost every instrument definition wraps its oscillator/sample in. |
| `<amplifier>` | Flat gain. `gain` (dB). |
| `<tremolo>` | Amplitude LFO. `frequency` (Hz), `amplitude` (0.0-1.0 depth), `aftertouch` (boolean - depth follows channel pressure instead of being fixed). |
| `<biquadFilter>` | A single biquad (see `dsp/Biquad.h`'s `FilterType`). `type`, `fc` (0.0-0.5, normalized), `Q`, `peakGainDB`, `aftertouch` (boolean - `fc` follows channel pressure). |
| `<resonantFilter>` | Moog-style resonant lowpass (`dsp/MoogVCF.h`). `cut`/`cutmin`/`cutmax`, `res`, `aftertouch`. |
| `<tapeDegradation>` | Tape/media degradation - wow/flutter, hiss, dropouts, saturation. See `docs/tape_degradation.md` for the full reference; it's also the one effect here where the track/voice distinction above changes its *character*, not just its scope - worth reading if you only read one section of that page. |

Only `<tapeDegradation>` has its own dedicated reference page so far -
the rest are documented here at the attribute-name level; read the
corresponding `effects/*.h`/`.cpp` for exact defaults and ranges if
something isn't obvious from the name.

## Position, sends, and other track-only attributes

`azimuth`/`elevation`/`distance`/`extent`, `sendA`/`sendB`/`sendMain`,
`solo`/`mute`, and `id` only ever mean something on a real `<track>`
element itself (`InstrumentTrack`) - none of the wrapper effects above
have their own position or sends. A voice-attached effect's *output*
position is always just whatever position the note it's wrapping was
already given; wrapping a track-attached effect around an `<track>`
doesn't move that track, the position stays on the `<track>` element
underneath. `<tapeDegradation>` is the one exception - see its own page.
