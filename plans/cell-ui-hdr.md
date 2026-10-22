# Cell UI: an HDR and wide-gamut colour model

Effects (`plans/cell-ui-effects.md`) are where HDR comes from: bloom around an
LED, light scattered through a translucent slab, ambient occlusion in a
crease. Flat cells and text stay SDR in look. This plan is the colour model
that lets any colour in the library be brighter than white or more saturated
than sRGB, and the renderer choice that lets one renderer show it while the
others clip it. The effects renderer uses Vulkan on Linux and Direct3D on
Windows, because those are the APIs that can present HDR.

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
   index, say). Its components are never negative (below), which the physical
   lighting model depends on.
2. **Clipping is the sink's job.** What can show only sRGB (the terminal, the
   SDR window, canvas images) converts at the last step with one function,
   `Color::toSrgb8()`. Nothing upstream knows or cares which sink it has.
3. **Values are relative to SDR white.** 1.0 is the display's SDR reference
   white, whatever its nits; 4.0 is four times the light. The UI never speaks
   in nits; the presenter turns relative into absolute.
4. **The renderer is chosen once, at start, from what the machine can do.**
   No switching, no fallback inside a frame (below). Neither renderer is "the
   SDR one": the effects renderer shows SDR displays too.

## The model

### `cellui::Color`

Three floats: **linear light, Rec.2020 primaries, 1.0 = SDR white**. The
lighting model (ambient occlusion, scattering, bloom) adds, scales and
multiplies colours as light, and a negative component would be light taking
energy away; Rec.2020 contains every colour a display can show, sRGB and
Display-P3 included, so **no component is ever negative**. It is also what
HDR10 carries (PQ-encode it and send it), and a matrix from the scRGB that a
Windows float swap chain takes. A value above 1 is HDR.

- sRGB and Display-P3 colours are positive in Rec.2020 (sRGB's green
  `(0, 1, 0)` is `(0.3293, 0.9195, 0.0880)`); Rec.2020's own primaries are
  `(1,0,0)`, `(0,1,0)`, `(0,0,1)`.
- A colour outside Rec.2020 (an OKLCH with a huge chroma, ProPhoto) is gamut
  mapped *when the `Color` is constructed* (chroma reduced at constant hue),
  so the invariant holds without every operation checking it. `mix` with
  `t` in [0,1], and a positive gain, keep it. A debug build asserts it.
- The luminance weights are Rec.2020's: `Y = 0.2627 R + 0.6780 G + 0.0593 B`.

```
Color(int r, int g, int b)            // sRGB 8-bit, as now: converts
Color(std::string_view)               // the notation below
static Color linear(float r, float g, float b);       // linear Rec.2020 itself, HDR if above 1
static Color displayP3(float r, float g, float b);    // gamma-encoded, as written
static Color rec2020(float r, float g, float b);      // gamma-encoded, as written
static Color fromOklch(float l, float c, float h);    // as now; out-of-gamut is mapped in
Color  operator*(float gain) const;   // lights it: c * 4 is four times the light
Color  mix(const Color &, float t) const;             // in linear light
float  luminance() const;             // relative to SDR white
std::array<uint8_t,3> toSrgb8() const;                // the sink: gamut-mapped, tone-mapped, encoded
float r() const; ...                  // the linear components
```

`getRed()/getGreen()/getBlue()` remain for now as the sRGB8 view (the same
`toSrgb8()` result), so call sites compile; each caller that *computes* with
them (blends, contrast tests, tints) moves to the linear API as it is
touched, and a grep for them is a step-1 task. An sRGB8 colour round-trips
through the float and back to the same bytes (tested over all 2^24), so hex
colours stay exact.

### The SDR sink (`toSrgb8()`)

In one function in one file, so every SDR sink agrees: map Rec.2020 into
sRGB's gamut, bring the range into [0, 1], encode with the sRGB transfer.
The placeholder is the plain version (matrix to Rec.709, pull chroma in
along constant OKLCH hue until it fits, scale to `max <= 1`); the real
mapping comes from the tone-mapping work and replaces the body without
changing the signature. The effects renderer's own output stage (HDR out, or
tone-mapped for an SDR display) is a function of the same family, in the
renderer, not here. In-gamut colours with every component at most 1 must map
to themselves.

### Notation

Only what CSS Color 4 defines, parsed by `Color(std::string_view)`:
`#rrggbb`, `oklch(L C H)`, and `color(srgb | srgb-linear | display-p3 |
rec2020 r g b)`. Nothing of ours is added: HDR magnitude is set from code,
`Color(...) * 6` or `Color::linear(...)`, and a theme that wants a glowing
token gives its base colour in text and the gain beside it as a number. Nits
are never accepted. (`color(rec2020 0 1 0)` is a standard form: the Rec.2020
green primary, gamma-encoded; in this model `(0, 1, 0)` linear.)

**API.** Cells take colours as they do today, now wide-capable:

```
plane->setFgColor(Color), setBgColor(Color)   // new overloads; (int,int,int) stays
```

`CellBuffer::Cell`/`Style` keep `std::optional<Color> fg, bg` and gain no
fields; `Color` is 12 bytes where it was 3, which is small beside the cell's
`std::string`. The LED in the middle of a volumetric button is a cell with
`bg = Color("oklch(0.8 0.2 25)") * 6`: the terminal and the basic window show
a clipped, pale red cell; the effects renderer sees a value of 6 and its
bloom pass lights the surroundings. No "emissive" attribute exists:
*emissive is a colour above 1*, and bloom's threshold is 1.

### Compositing: always in linear light

`BufferPlane::compose()` computes `bg_blend`, `fg_contrast`, backdrop dims
and tints, and every blend in `Color` in linear light, for every backend,
SDR included. One rule means one result: a blend that gave different answers
for a terminal and for the effects renderer would make every effect depend on the
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
- Glyph anti-aliasing is not a `Color` operation. A rasteriser draws a glyph's
  outline as a *coverage* per pixel, 0 to 1 along the edge, and the pixel is
  `mix(bg, fg, coverage)`. canvas does that mix in 8-bit gamma-encoded sRGB
  and that stays for the basic renderer. The effects renderer keeps the
  coverage mask in its glyph atlas and mixes in linear light, which makes
  light-on-dark text look bolder and dark-on-light thinner than the gamma mix
  does; a contrast adjustment on the coverage is tuned by eye if it shows.

Equality and hashing (the frame diff, backdrop tile fingerprints, the
window's texture keys) compare the floats bitwise, so equal compositions give
equal colours; the composer is deterministic, so that is enough.

## Choosing the renderer

Two renderers, named for what they are, not for the dynamic range they show:

- **`basic`**: the platform's own drawing, 8-bit: GSK on Linux, Direct2D on
  Windows, cells in the terminal. It reads `fg`/`bg` through `toSrgb8()`, draws
  no effect attributes and knows nothing of HDR.
- **`effects`**: the GPU renderer: Vulkan on Linux, Direct3D on Windows. A
  float framebuffer, the cells and glyph atlas, the lighting model (ambient
  occlusion, subsurface scattering, bloom) and an output stage. The output
  stage is **HDR when the display reports HDR** and tone-mapped SDR when it
  does not, so the effects look right on every display; HDR is its best
  output, not a requirement for it.

Why those APIs: HDR reaches a display through the swap chain's colour space
(Vulkan's `VK_EXT_swapchain_colorspace`, DXGI's colour spaces), which OpenGL
has no portable way to ask for (on Linux, none in practice).

At start, before any window is shown, the window backend probes and decides
for the run: `--renderer basic|effects|auto`, `auto` by default.

```
auto: effects if
        - a Vulkan device (Linux) / Direct3D device (Windows) can be created
          with the features the renderer needs (floor to be fixed when the
          shaders exist), and it is a hardware device (not lavapipe or
          another CPU device, not WARP), and the driver is not on the
          known-bad list;
      else basic
```

- The terminal is always `basic`: it has no GPU.
- Nothing falls back during a frame or between frames. If the chosen renderer
  fails after the choice (device lost, shader error), that is an error
  reported and the program exits, not a silent change of look; `--renderer
  basic` is the user's escape. A CPU device fails the probe in advance, so a
  machine that would crawl is never given `effects`.
- **What can still change at run time is the display, not the renderer**:
  HDR toggled in the OS, a window moved to another monitor. `effects` has
  both outputs, so it re-reads the display's capabilities on the change event
  and rebuilds its swap chain between frames, inside the renderer. If
  startup-only is simpler to ship, the first version logs "restart to apply".
- **Blur.** The current blur (a GSK node, `term::BackdropEmulation`'s box
  passes) stays as it is for `basic` and the terminal; it blurs `Color`s
  clipped at the end, nothing else. `effects` has its own separable blur on
  float textures, the same pass bloom uses (threshold at 1.0, downsample
  chain), so a blurred backdrop there keeps its highlights. The two are not
  made to look identical.
- **Pixel regions** (the scopes, canvas images) stay 8-bit sRGB: canvas stays
  SDR. In `effects` they are sampled as sRGB textures, linearised and may be
  multiplied by a gain.
- **Two GPU back ends is a cost.** The renderer's shaders and passes are
  written once, in one shading language, and compiled for both (SPIR-V for
  Vulkan, DXIL or DXBC for Direct3D); a thin device layer hides the API.
  Whether that layer is ours or SDL3's GPU API (which wraps both, exposes
  HDR swap-chain compositions, and can be given a native window to draw into)
  is decided by a spike; see Open questions.

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

The terminal and `basic` leave it unset. `peak` is relative so an effect can
scale to the display (a 4x LED on a 1.5x display is simply clipped).
`BackendEvent` carries a display change, as it does a resize.

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
- Pure tests with no backend: notation parsing (each CSS form; rejects
  nits and anything else); matrix round trips (sRGB, P3 and Rec.709 to Rec.2020
  and back within 1e-4; sRGB green is `(0.3293, 0.9195, 0.0880)`); no component
  is ever negative after construction (random OKLCH, including huge
  chroma); the sRGB8 round trip over all 2^24 values;
  `toSrgb8()` identity for in-gamut colours; the compose rule in linear light
  (the 50% black/white case); equality and hashing.
- `effects` output is checked offscreen on a CPU device (lavapipe on Linux,
  WARP on Windows, forced past the probe by a test-only flag), float read
  back, with a tolerance, in the gallery's golden style. CI cannot show HDR,
  only check the values.

## Should GTK go?

Not decided; the Vulkan requirement narrows the question.

**What GTK gives**: the menu bar, dialogs, file choosers, input methods and
the accessibility tree on Linux (what `plans/cell-ui-window.md` chose it
for). Replacing it means writing those ourselves or taking SDL3, which has
none of them either.

**What changed**: GTK 4 has no API for an application to put its own Vulkan
swap chain in a widget (its Vulkan is internal to GSK, and `GtkGLArea` is
OpenGL, which cannot present HDR). The only way to put the `effects`
renderer in a GTK window is a **native child surface of ours**: on Wayland a
`wl_subsurface` of the window's `wl_surface` (via `gdk_wayland_surface_
get_wl_surface()`), with the Vulkan WSI on it and colour management set on it
(`wp_color_management_v1`, through Mesa's WSI where it supports it); on X11 a
child window, which gets Vulkan but never HDR. GTK keeps the menu bar,
dialogs and input; our surface carries the whole grid. The cost: our surface
and GTK's must stay in step (size, scale, focus, input routed from GTK's
events or taken from our surface), and popups that must float over the grid
are GTK's, not ours, so effects do not reach them.

So the spike has two arms, and the choice follows the measurement:

1. **GTK window + Vulkan child surface** on a Wayland HDR setup (a current
   GNOME or KDE). Succeeds if HDR presents, resizing and scale changes do not
   tear or lag, and input and focus are clean. Linux HDR *output* depends on
   the compositor's colour management; elsewhere `effects` runs with SDR
   output.
2. **SDL3 host** (SDL3 creates the Vulkan surface, exposes HDR window
   properties, and its GPU API could be the device layer above), with Cell
   UI's own menus, dialogs, file dialogs and accessibility on top. Taken only
   if arm 1 fails or is too fragile **and** Linux HDR output is worth losing
   GTK's chrome.

Windows has no such question: the Win32 window owns its swap chain.

## Steps

1. **`Color` becomes linear float Rec.2020** (`cellui/Color.h`): constructors,
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
5. **Renderer choice**: the probe, `--renderer`, the CPU-device and
   bad-driver tests; only `basic` exists, so the seam is proved.
6. **Device layer spike**: one shading language compiled for Vulkan and
   Direct3D, and ours versus SDL3's GPU API for the layer; a triangle in an
   FP16 target presented on both. Decides the shape of step 7.
7. **`effects` renderer, tone-mapped SDR out**: cell quads, glyph atlas,
   float framebuffer, the shared blur. The gallery renders through it
   offscreen on a CPU device.
8. **Host spike on Linux** (above), then HDR output there if it passes.
9. **Windows HDR output** (`plans/cell-ui-window.md`): needs the Win32
   backend first.
10. **Effects** (`plans/cell-ui-effects.md`): bloom first (wide colours and
   the blur), then ambient occlusion and subsurface scattering, which need
   the cube model's effect attributes (height or normal and a thickness per
   cell, set through the plane API).
11. **Scopes**: a plus, last. Their 8-bit canvas images stay; in `effects` a gain
    on the image lights peaks and the clip red above 1 (`Image::gain`, applied
    by the sampler). Float images from `SpectrumImage`/`HeatmapImage`/
    `VuMeterImage` (hand-drawn already) only if 8 bits show banding in the
    heatmap's dark end.
12. **Theme**: wide variants for the tokens that glow (playhead, the LED hues
    shared with the Launchpad, the clip pulse's peak), written in the
    notation above.

## Open questions

- The real `toSrgb8()` mapping (curve, gamut mapping), and whether the same
  curve is the `effects` output stage on an SDR display: from the
  tone-mapping work. Only the signature (a pure function of one `Color`, no
  state) is needed in advance; the placeholder is enough until the gallery
  swatch page is looked at by eye. It does not block steps 1 to 5.
- Direct3D 11 or 12 on Windows, and the Vulkan floor on Linux: decided with
  the device layer spike. 11 is simpler; 12 is the one SDL3's GPU API wraps.
- Whether `effects` follows display changes at run time or the first version
  applies them at restart.
