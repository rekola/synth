# Cell UI: an HDR and wide-gamut colour model

Effects (`plans/cell-ui-effects.md`) are where HDR comes from: bloom around an
LED, light scattered through a translucent slab, ambient occlusion in a
crease. Flat cells and text stay SDR in look. This plan is the colour model
that lets any colour in the library be brighter than white or more saturated
than sRGB, and the renderer choice that lets one renderer show it while the
others clip it.

Status: under discussion. Nothing is built. This is a green-field change:
goldens that move are regenerated, not worked around.

"HCG" in the request is read as **WCG**, wide colour gamut (Display-P3,
Rec.2020). HLG and PQ are output encodings the presenter picks; they are not
part of the model.

## Principles

1. **One colour type, and it can hold any colour.** `cellui::Color` becomes
   linear-light float RGB, extended range. There is no second "wide" type and
   no per-cell SDR copy: a cell's `fg` and `bg` are `Color`s and may be wide.
   A narrow type is added only if something shows it is needed (a palette
   index, say).
2. **Clipping is the sink's job.** What can show only sRGB (the terminal, the
   SDR window, canvas images) converts at the last step with one function,
   `Color::toSrgb8()`. Nothing upstream knows or cares which sink it has.
3. **Values are relative to SDR white.** 1.0 is the display's SDR reference
   white, whatever its nits; 4.0 is four times the light. The UI never speaks
   in nits; the presenter turns relative into absolute.
4. **The renderer is chosen once, at start, from what the machine can do.**
   No switching, no fallback inside a frame (below).

## The model

### `cellui::Color`

Three floats: **linear light, Rec.709 primaries, extended range, 1.0 = SDR
white** (scRGB). It is what a Windows HDR swap chain takes natively, what an
FP16 OpenGL framebuffer holds, and a matrix away from Display-P3 and
Rec.2020. A colour above 1 is HDR; a colour outside sRGB's gamut has a
negative component. Linear Rec.2020 (no negatives) was rejected: every sRGB
colour in the theme would then be a matrix product away from what a designer
wrote, and the swap chain wants scRGB anyway.

**The greenest Rec.2020 green** (`color(rec2020 0 1 0)`) is, in this model,

```
r = -0.5876   g = 1.1329   b = -0.1006        (Y = 0.678)
```

The red and blue are negative, because no mix of sRGB's primaries makes it,
and the green is above 1 because Rec.2020's green primary is farther out than
sRGB's, so the same luminance fits in a smaller gamut only by overshooting.
Pure Rec.2020 red is `( 1.6605, -0.1246, -0.0182)` and blue
`(-0.0728, -0.0083, 1.1187)`. Every colour a display can show is a point in
this space; clipped to sRGB, that green becomes `(0, 1, 0)`, ordinary sRGB
green. The conversion is the matrix

```
 1.6605 -0.5876 -0.0728
-0.1246  1.1329 -0.0083     (linear Rec.2020 -> linear Rec.709)
-0.0182 -0.1006  1.1187
```

and Display-P3's follows the same way from its primaries. Floats hold all of
it with room to spare; negatives are legal in a `Color`, and arithmetic does
not clamp them (a clamp is the sink's act).

```
Color(int r, int g, int b)            // sRGB 8-bit, as now: converts to linear
Color(std::string_view)               // the notation below
static Color linear(float r, float g, float b);   // scRGB itself
static Color displayP3(float r, float g, float b);    // gamma-encoded, as written
static Color rec2020(float r, float g, float b);
static Color fromOklch(float l, float c, float h);    // as now; chroma may exceed sRGB
Color  operator*(float gain) const;   // lights it: *4 is four times the light
Color  mix(const Color &, float t) const;             // in linear light
float  luminance() const;             // Rec.709 Y, relative to SDR white
std::array<uint8_t,3> toSrgb8() const;                // the sink: gamut-mapped, clipped, encoded
float r() const; ...                  // the linear components
```

`getRed()/getGreen()/getBlue()` remain for now as the sRGB8 view (the same
`toSrgb8()` result), so call sites compile; each caller that *computes* with
them (blends, contrast tests, tints) moves to the linear API as it is
touched, and a grep for them is a step-1 task. An sRGB8 colour round-trips
through the float and back to the same bytes (tested over all 2^24), so hex
colours stay exact.

### The SDR sink (`toSrgb8()`)

In one function in one file, so every SDR sink agrees: bring the colour into
sRGB's gamut, bring its range into [0, 1], encode with the sRGB transfer. The
placeholder is the plain version (pull chroma in along constant OKLCH hue
until it fits; then scale to `max <= 1`); the real mapping comes from the
tone-mapping work and replaces the body without changing the signature.
The GL renderer's own output stage (HDR out, or tone-mapped to SDR when the
display is not HDR) is a second function of the same family, in the GL
renderer, not here.

### Notation

Three places want one: source (theme, gallery, tests), the terminal (never;
it gets `toSrgb8()`) and the API.

**Text**, parsed by `Color(std::string_view)`: CSS Color 4's functional
notation where it has an equivalent, plus one suffix for light.

| Written | Meaning |
|---|---|
| `#ff8040` | sRGB, as now |
| `oklch(0.75 0.15 60)` | as `Color::fromOklch()` |
| `color(srgb-linear 2 0.5 0.1)` | scRGB itself; above 1 is HDR |
| `color(display-p3 1 0.2 0.1)` | gamma-encoded P3, as written in CSS |
| `color(rec2020 0.8 0.1 0.1)` | gamma-encoded Rec.2020 |
| *any of the above* `*4` | multiplies the linear light by 4: `#ff8040*4`, `oklch(0.8 0.2 25)*3` |

`*N` is the HDR notation proper: relative, applicable to any base colour,
and strip-able (without the suffix what is left is the SDR colour). CSS's
`rec2100-pq`/`rec2100-hlg` are absolute encodings; accepted for import only
if an asset needs them. Nits are not accepted.

**API.** Cells take colours as they do today, now wide-capable:

```
plane->setFgColor(Color), setBgColor(Color)   // new overloads; (int,int,int) stays
```

`CellBuffer::Cell`/`Style` keep `std::optional<Color> fg, bg` and gain no
fields; `Color` is 12 bytes where it was 3, which is small beside the cell's
`std::string`. The LED in the middle of a volumetric button is a cell with
`bg = Color("oklch(0.8 0.2 25)*6")`: the terminal and the SDR window show a
clipped, pale red cell; the GL renderer sees a value of 6 and its bloom pass
lights the surroundings. No "emissive" attribute exists: *emissive is a
colour above 1*, and bloom's threshold is 1.

### Compositing: always in linear light

`BufferPlane::compose()` computes `bg_blend`, `fg_contrast`, backdrop dims
and tints, and every blend in `Color` in linear light, for every backend,
SDR included. One rule means one result: a blend that gave different answers
for a terminal and for the GL renderer would make every effect depend on the
sink, and no clip could then be trusted to look like what composed it.
Consequences, accepted:

- Blends read lighter and more physical than the gamma-space averages of
  today (a 50% mix of black and white is sRGB 188, not 128; a 30% dim toward
  a dark tint leaves darks brighter). Dim and blend amounts in the theme are
  retuned once, by eye on the gallery, and the goldens regenerated.
- The terminal's backdrop emulation (`term::BackdropEmulation`) averages
  cells in linear light too; it already works on colours, so only its
  arithmetic moves.
- Not everything is a blend: **gradients and hue ramps interpolate in
  OKLab** (`Color::mix()` takes the space, default linear light; a
  `Gradient` says OKLab), since linear-light interpolation of two saturated
  hues is muddy. Compositing is physical, design interpolation is perceptual.
- Glyph anti-aliasing is not a `Color` operation: canvas rasterises text in
  8-bit sRGB and that stays. The GL renderer's atlas coverage is applied in
  linear light and gets a gamma-ish contrast adjustment if light-on-dark text
  looks heavy; decided by eye.

Equality and hashing (the frame diff, backdrop tile fingerprints, the
window's texture keys) compare the floats bitwise, so equal compositions give
equal colours; the composer is deterministic, so that is enough.

## Choosing the renderer

At start, before any window is shown, the window backend probes and decides
for the run:

```
renderer = --renderer sdr|gl|auto        (auto by default)
auto: gl if  - a GL 4.x context can be created (4.3 core is the floor; the
               shaders say if 4.5 buys anything), and
             - it is a hardware driver (renderer string is not llvmpipe, a
               software rasteriser or a basic render driver), and
             - the driver is not on the known-bad list;
      else sdr
```

- **`sdr`** is today's renderer: GSK on Linux, Direct2D on Windows, cells in
  the terminal. It reads `fg`/`bg` through `toSrgb8()` and ignores effect
  attributes and HDR.
- **`gl`** is the effects renderer: an FP16 framebuffer, the cells and glyph
  atlas, bloom, ambient occlusion, subsurface scattering, and an output stage.
  The output stage is **HDR (scRGB or HDR10) when the display reports HDR**,
  and otherwise a tone-mapped SDR output: the effects look right on any
  display; HDR is the best output of the renderer, not a requirement for it.
- Nothing falls back during a frame or between frames. If the chosen
  renderer fails after it has been chosen (lost device, shader error), that
  is an error reported and the program exits, not a silent change of look;
  `--renderer sdr` is the user's escape. A software GL fails the probe in
  advance, so a machine that would crawl is never given `gl`.
- **What can still change at run time is the display, not the renderer**:
  HDR toggled in the OS, a window moved to another monitor. The `gl` renderer
  already has both outputs, so it re-reads the display's capabilities on the
  change event and switches the *output stage* between frames (on Windows
  that is a swap-chain rebuild, done between frames, inside the renderer).
  If startup-only turns out simpler to ship, the first version does that and
  logs "restart to apply".
- **Blur.** The current blur (a GSK node, `term::BackdropEmulation`'s box
  passes) stays as it is for the `sdr` renderer and the terminal; it now
  blurs wide colours clipped at the end, nothing else. The `gl` renderer has
  its own separable blur on float textures, the same pass bloom uses
  (threshold at 1.0, downsample chain), so a blurred backdrop there keeps its
  highlights. The two are not made to look identical.
- **Pixel regions** (the scopes, canvas images) stay 8-bit sRGB: canvas stays
  SDR. In the `gl` renderer they are sampled as sRGB textures, linearised and
  may be multiplied by a gain.

### Capabilities, read in advance

`DisplayInfo` gets what the choice and the effects need:

```
struct HdrInfo {
  bool available = false;       // the display shows HDR right now
  float peak = 1;               // peak luminance in SDR whites (nits / sdr_white_nits)
  float sdr_white_nits = 0;     // 0 unknown
  Gamut gamut = Gamut::SRGB;    // SRGB, DISPLAY_P3, REC2020: the display's primaries
};
std::optional<HdrInfo> hdr;     // unset: the backend has no HDR path at all
```

The terminal and the SDR renderers leave it unset. `peak` is relative so an
effect can scale to the display (a 4x LED on a 1.5x display is simply
clipped). `BackendEvent` carries a display change, as it does a resize.

## The test backend

`TestBackend` keeps the composed frame and `FrameInfo`; with `Color` wide
there is nothing to add to carry colour: `frame()` cells hold exactly what
composition produced.

- `sdrFrame()` returns the frame's cells passed through `toSrgb8()`, i.e.
  what the terminal shows, so a test checks both halves ("the compose gave
  `bg` luminance 6" and "the sink clipped it to this").
- `setHdr(HdrInfo)` / `setHdr(std::nullopt)` set what `info().hdr` reports and
  emit the display change event, so a test checks the application reacts to
  HDR appearing and going away.
- Pure tests with no backend: notation parsing (every row of the table;
  rejects nits); matrix round trips (P3 and Rec.2020 to scRGB and back within
  1e-4; the green above); the sRGB8 round trip over all 2^24 values;
  `toSrgb8()` identity for in-gamut colours; the compose rule in linear light
  (the 50% black/white case); equality and hashing.
- `gl` output is checked offscreen against a software GL (llvmpipe, forced
  past the probe by a test-only flag), FP16 read back, with a tolerance, in
  the gallery's golden style. CI cannot show HDR, only check the values.

## Should GTK go?

Not now; a spike answers it.

**What GTK gives**: the menu bar, dialogs, file choosers, input methods and
the accessibility tree on Linux (what `plans/cell-ui-window.md` chose it
for). Replacing it means writing those ourselves or taking SDL3, which has
none of them either.

**What it does not give yet**: Ubuntu 24.04's GTK is 4.14, the oldest
supported, and has no colour-state or float-texture API for this (4.16
brought `GdkColorState` and FP16 memory textures; the compositor-facing
side, Wayland's colour-management protocol, came later and only where the
compositor has it). X11 has no HDR. So **HDR output on Linux is a
newer-distro, Wayland feature** whatever sits under it, and the probe above
simply reports `hdr` unset elsewhere: the `gl` renderer then tone-maps to SDR
and the effects still work.

**What the `gl` renderer needs from the host**: a GL context in a surface, an
FP16 or 10-bit format, and a way to tell the compositor the surface is
scRGB/PQ. `GtkGLArea` gives the first; the rest depends on the GTK and
compositor of the day. Spike, on a Wayland HDR setup, in order:

1. `GtkGLArea` with an FP16 framebuffer: does GTK composite it as HDR or
   tone-map it? Can the area be given a colour state?
2. If not, a Wayland subsurface of ours under the GTK window with its own GL
   context and `wp_color_management_v1` set directly. Wayland only; the main
   cost.
3. If both fail on the GTK versions in use: Linux HDR *output* is off (the
   probe reports no HDR) until they do; the `gl` renderer's SDR output stays.
   That is not a reason to change toolkit.

Switch away only if both routes fail **and** Linux HDR output is worth losing
GTK's menus, dialogs and accessibility; then it is SDL3 with the same `gl`
renderer (SDL3 exposes HDR window properties and float swap chains) and
Cell UI's own menus and dialogs: a large step, taken with eyes open. The
renderer is a separate thing from the host, so that swap changes neither the
colour model nor the effects.

## Steps

1. **`Color` becomes float scRGB** (`cellui/Color.h`): constructors,
   conversions, `toSrgb8()` with the placeholder clip, notation, `*gain`,
   `mix`. The `getRed()` family kept as the sRGB8 view; the grep of callers
   that compute with them, each moved over. Unit tests above. Every goldens
   that moves is regenerated and eyeballed.
2. **Linear compositing** in `BufferPlane`, `Backdrop`, `Band` and
   `term::BackdropEmulation`; theme amounts retuned on the gallery; goldens
   regenerated.
3. **Cells take wide colours**: `setFgColor(Color)`/`setBgColor(Color)`,
   `describe()`, hashing; every sink (`FrameEncoder`, `toAnsi()`, the
   window's rasteriser, the canvas rasteriser) goes through `toSrgb8()`; the
   gallery gets a swatch page (a colour and its clip, side by side, including
   the Rec.2020 green and a `*6` LED).
4. **`DisplayInfo::hdr`**, the display-change event, `TestBackend`
   `setHdr()`/`sdrFrame()`; terminal and SDR window report none.
5. **Renderer choice**: the probe, `--renderer`, the software-driver and
   bad-driver tests; only `sdr` exists, so the seam is proved.
6. **`gl` renderer, tone-mapped SDR out**: cell quads, glyph atlas, float
   framebuffer, the shared blur. The gallery renders through it offscreen.
7. **Host spike on Linux** (above), then HDR output there if it passes.
8. **Windows HDR output** (`plans/cell-ui-window.md`): needs the Win32
   backend first.
9. **Effects** (`plans/cell-ui-effects.md`): bloom first (wide colours and
   the blur), then ambient occlusion and subsurface scattering, which need
   the cube model's effect attributes (height or normal and a thickness per
   cell, set through the plane API).
10. **Scopes**: a plus, last. Their 8-bit canvas images stay; in `gl` a gain
    on the image lights peaks and the clip red above 1 (`Image::gain`, applied
    by the sampler). Float images from `SpectrumImage`/`HeatmapImage`/
    `VuMeterImage` (hand-drawn already) only if 8 bits show banding in the
    heatmap's dark end.
11. **Theme**: wide variants for the tokens that glow (playhead, the LED hues
    shared with the Launchpad, the clip pulse's peak), written in the
    notation above.

## Open questions

- The real `toSrgb8()` mapping: from the tone-mapping work. Only the
  signature (a pure function of one `Color`, no state) and its one file are
  needed from it; the placeholder is enough until step 3 is looked at by eye.
  It does not block steps 1 to 5.
- Whether `gl`'s output stage follows display changes at run time or the first
  version applies them at restart.
- The GL floor: 4.3 is a guess until the shaders exist.
