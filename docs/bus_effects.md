# The shared send bus

Every song has one shared, always-on effects bus with two slots, **A** and
**B**. Each `<track>` sends a copy of its own dry signal into the bus via
`sendA`/`sendB` attributes (0.0-1.0, default 0.0 - a track sends nothing
by default):

```xml
<track id="0" instrument="0" sendA="0.3" sendB="0.5"/>
```

Slot B is processed first each block, then whatever it produces can
additionally feed into slot A that same block (see "Chain send" below),
then slot A's own output (plus whatever reached it from B) is mixed into
the song. Both slots are spatial: whichever effect occupies a slot
produces several independent "taps," each placed at its own point in
space around the listener - a reverb's taps are its 8 decorrelated
feedback lines, a delay's are its 4 echoes, a granular cloud's are its
individual grains.

## Configuring the bus

The compiled-in default (no `<bus>` element at all) is reverb in slot A
and delay in slot B, each at its own default settings. To change this,
add a `<bus>` element to the song, directly under `<song>`:

```xml
<bus>
  <reverb preset="hall"/>
  <granular preset="cloud"/>
</bus>
```

A few rules that are easy to get wrong:

- **Position, not an attribute, decides which slot a child is.** The
  first child of `<bus>` is always slot A, the second is always slot B.
- **`<bus>`'s presence fully replaces the default bus** - it does not
  merge with it. A `<bus>` with only one child leaves slot B *empty*, not
  "still the default delay." To configure only slot B while leaving slot
  A genuinely empty, write `<none/>` explicitly as the first child:
  ```xml
  <bus>
    <none/>
    <delay preset="dub"/>
  </bus>
  ```
- An unrecognized child element name falls back to that slot's own
  compiled default type (reverb for A, delay for B) rather than going
  silent - useful if a song file references an effect type that doesn't
  exist yet in the running build.
- Omitting `<bus>` entirely, or writing `<bus/>` with no children, leaves
  both slots at the compiled default and empty respectively as
  appropriate - the editor never writes out a `<bus>` element for a song
  that's still exactly at the compiled default (reverb in A, delay in B,
  both unmodified).

## Attributes every bus effect shares

- **`wet`** (0.0-1.0): how loudly this slot's output is mixed into the
  final signal. Each effect type has its own tuned default.
- **`chainSend`** (0.0-1.0, default 0.3): how much of this slot's output
  additionally feeds into slot A (inert on slot A itself, since nothing
  sits after it). The default lets a bit of whatever's in slot B pick up
  slot A's coloring "for free" - e.g. delay echoes gaining a natural
  reverb halo.
- **`preset`**: see below. Individual attributes (`size=`, `density=`,
  ...) always override whatever a preset implies, so `preset="hall"
  decay="6.0"` means "the hall preset, but with a 6-second decay."

## `<reverb>` - FDN spatial reverb

An 8-line feedback delay network; its 8 decorrelated tap outputs are
spread around the listener rather than mixed down to stereo.

| Attribute | Range | Meaning |
|---|---|---|
| `size` | 0.1-3.0 | Room-size multiplier. 1.0 is the compiled-in base spread. |
| `decay` | ≥0.01s | RT60 - time for the tail to decay 60dB. |
| `damping` | 0.0-1.0 | 0 = bright/undamped, 1 = heavily damped/dark. |
| `preDelay` | 0.0-0.2s | Gap before the first reflection arrives. |

Presets:

- **default** (no `preset` needed) - a medium hall: balanced size, decay,
  and damping, a modest pre-delay. Deliberately unremarkable - a
  reasonable starting point, not a showcase extreme.
- **room** - a small, tight, present space: short decay, more absorptive
  damping, almost no pre-delay, as if reflecting off close walls.
- **hall** - a real large hall: bigger, longer, and wetter than the
  default medium hall, with a touch more pre-delay for the extra distance
  to the first reflection.
- **cathedral** - huge and cavernous: maximum size, a very long decay,
  heavier damping for the murky darkness of reflections off distant stone
  surfaces, and a large pre-delay for the sheer distance sound travels
  before the first echo returns.
- **plate** - classic studio plate character: dense and bright (minimal
  damping), no real sense of physical room size, essentially no
  pre-delay - a smooth metallic wash rather than a simulated space.
- **ambient** - a slow, dark, heavily diffuse wash suited to pads and
  ambient production: very long decay, strong damping, and a pronounced
  pre-delay that separates the tail from the dry source instead of
  blending straight into it.

## `<delay>` - multi-tap delay

A single delay line read back at 4 fixed offsets (1x/2x/4x/8x a base
interval), each with its own fixed gain and position in space. Only the
longest (8x) tap feeds back into the line, so every tap still repeats
(quieter, darker) on later passes since they all read the same evolving
buffer.

| Attribute | Range | Meaning |
|---|---|---|
| `baseRows` | ≥0 | Base tap interval, in pattern rows. Accepts a fraction (`"3/16"`) or a decimal. |
| `feedback` | 0.0-0.95 | Gain applied to the longest tap's damped readout each pass. |
| `damping` | 0.0-1.0 | 0 = bright, 1 = dark, same convention as reverb. |
| `pattern` | `static`/`pingpong`/`orbit`/`recede` | How the longest tap's position in space evolves, once per pass (see below). |
| `patternSpeed` | - | Meaning depends on `pattern` - degrees/pass for `orbit`, percent shrink + elevation drop/pass for `recede`, unused by `static`/`pingpong`. |

Pattern modes: **static** leaves the longest tap's position fixed;
**pingpong** flips its left/right side every pass; **orbit** rotates it
continuously around the listener by `patternSpeed` degrees per pass;
**recede** shrinks its gain and drops its elevation every pass, so it
sinks away into the distance.

Presets (several are named after, and tuned to showcase, one of the
pattern modes above - not just that mode left at the flat defaults):

- **default** (no `preset` needed) - a "rich echo" at a musically
  ordinary interval, moderate feedback and damping, position fixed in
  space.
- **slapback** - a short, tight, mostly-single repeat: a brief interval,
  low feedback (one clear echo, only faint further repeats), bright
  (minimal damping), the way a live-room slapback sounds.
- **pingpong** - classic stereo ping-pong delay: several clearly audible
  bounces alternating side to side every pass.
- **orbit** - the longest tap continuously rotates around the listener,
  with enough feedback for several passes to actually complete a smooth,
  continuous spin.
- **recede** - the longest tap recedes into the distance: shrinking and
  darkening across several passes rather than cutting off abruptly.
- **dub** - long, heavily-damped, high-feedback dub-style repeats: each
  successive echo is noticeably darker than the last; position fixed in
  space, since a classic dub delay doesn't move.

## `<granular>` - granular cloud

Captures whatever reaches this slot into a rolling buffer and scatters
short windowed slices ("grains") of it back out, each with its own onset,
duration, pitch, amplitude, and position in space - up to 64 grains
sounding at once.

| Attribute | Range | Meaning |
|---|---|---|
| `grainSize` | 10-200ms | A single grain's duration. |
| `density` | 0-100/sec | Average grain trigger rate (overlapping, not a simultaneous-grain ceiling). |
| `scanPosition` | ≥0s | How far back from the live write head a grain starts reading, on average. |
| `scanJitter` | ≥0s | Random variation added to `scanPosition` per grain. |
| `pitchScatter` | 0-2400 cents | Per-grain playback-rate randomization (±). |
| `directionScatter` | 0-180° | Per-grain position randomization around `azimuth`/`elevation` (±, independent per axis). |
| `azimuth`, `elevation` | degrees | Center direction grains scatter around. |
| `amplitudeJitter` | 0.0-1.0 | Per-grain amplitude randomization. |
| `freeze` | boolean | Stop capturing new material, keep granulating whatever's already in the buffer - a sustained cloud from a snapshot rather than continuously-arriving input. |

Presets:

- **default** (no `preset` needed) - moderate grain size and density,
  gentle pitch scatter, a wide-but-front-biased direction scatter.
  Deliberately unremarkable - a balanced texture, not a showcase extreme.
- **shimmer** - a sparkling, localized texture: short grains at a brisk
  clip, a narrow scan window so it stays coupled to the note rather than
  dredging up older material, a touch of pitch scatter for sparkle, and a
  tight direction scatter so it reads as one point near the source.
- **cloud** - the enveloping, full-sphere-ish wash this effect is named
  for: medium grains, a generous scan window for variety, gentle pitch
  scatter, and a wide direction scatter so grains surround the listener.
- **glitch** - stuttery and digital: very short grains fired dense and
  fast from a near-repeat scan window, aggressive pitch scatter for jumpy
  pitch jumps, uneven amplitude, and a tight direction scatter so it reads
  as one glitching point.
- **wash** - long, slow-moving pad/drone material: long grains at a low
  trigger rate, a wide scan window drawing from a broad stretch of recent
  history, minimal pitch scatter (stays tonal), and a wide direction
  scatter for a spacious, ambient feel.
- **scatter** - scatters recognizable fragments of recent material back
  at moderate density from a wide scan window, with enough pitch scatter
  to be clearly audible as pitch-shifted echoes rather than pure texture -
  more "melodic debris" than `cloud`'s smoother wash.

### Why these numbers

`density × grainSize` is a preset's **overlap factor** - the average
number of grains sounding at once. Below 1, grains don't even touch each
other: there's a real silence gap between them, an audible gate/stutter
at the trigger rate, not a texture at all. Between 1 and about 2.5, grains
do overlap but not densely enough to hide the grain window's own shape,
so the total level still ripples up and down at the trigger rate instead
of staying smooth. Every preset below sits at 2.5 or above for exactly
this reason - it's a measured threshold (from a real diagnosed case: a
default that used to sit at overlap 0.9), not a stylistic choice, and
`density` is floored upward automatically if a combination would
otherwise fall short (grain size is never shrunk to compensate, since
that's the parameter most tied to a preset's actual character).

| Preset | grainSize | density | overlap | pitchScatter | Why |
|---|---|---|---|---|---|
| default | 60ms | 45/s | 2.7 | ±40¢ | Grain-like but not extreme in either direction; overlap comfortably above the floor without being dense; pitch scatter audible as gentle chorus-like detuning (unlike an earlier ±15¢, which was below the threshold of perception). |
| shimmer | 25ms | 100/s | 2.5 | ±60¢ | Shortest grain the density ceiling (100/s) allows while still clearing the overlap floor - "brisk clip" and brightness both want short+dense; scatter audible as sparkle without dominating. |
| cloud | 70ms | 50/s | 3.5 | ±35¢ | Deliberately the densest preset - the one explicitly named for continuous envelopment; scatter stays gentle so the cloud reads as related to the source, not aggressively shifted. |
| glitch | 25ms | 100/s | 2.5 | ±1000¢ | Same grain/density profile as shimmer (still respects the overlap floor - see below) - the "stuttery, digital" character comes entirely from a near-repeat scan window, dramatic pitch jumps, and uneven amplitude, not from gating. |
| wash | 180ms | 15/s | 2.7 | ±10¢ | Genuinely low trigger rate (15/s), but each grain is long enough to bridge the gaps and keep overlap smooth; pitch scatter deliberately minimal - the one preset where staying tonal means the *absence* of audible pitch movement is the goal. |
| scatter | 50ms | 55/s | 2.75 | ±250¢ | Grains long enough to read as recognizable fragments, not buzz; pitch scatter clearly audible as genuine pitch-shifted echoes (not mere detuning), short of glitch's chaos. |

**glitch** deliberately does *not* reach for "stuttery" via low overlap
(deliberate gating) - that would just reintroduce the gating bug the
overlap floor exists to prevent. Instead it uses the same tight
grain/density combination as `shimmer` and gets its digital-glitch
character from the *other* parameters: `scanJitter` is only 5ms (grains
draw from an almost-identical recent instant), `pitchScatter` is ±1000
cents (dramatic, unmistakable jumps, not detuning), and `amplitudeJitter`
is 0.6 (strongly uneven). One real interaction worth knowing: at ±1000
cents, roughly half of all grains play back faster than real time, which
requires them to start reading further back than the configured 5ms
window to avoid catching up to live audio mid-grain (see `dsp/
GranularEngine.cpp`'s own catch-up-floor logic) - so the *effective* scan
window for those grains is closer to ~20ms than 5ms. This is expected,
safe behavior, not a bug.

## Room coloring and chain send

Both `<delay>` and `<granular>` leave `chainSend` at its shared default
(0.3), so a bit of whatever's in slot B picks up slot A's coloring by
default - echoes (or grains) happening in the same simulated room as
everything else, not routed around it. Raise `chainSend` for more of that
coloring, or set it to `0.0` for a slot B effect that should stay
completely dry of slot A.
