# Cell UI: an HDR and wide-gamut colour model

Effects (`plans/cellui-effects.md`) are where HDR comes from: bloom around an
LED, light scattered through a translucent slab, ambient occlusion in a
crease. Flat cells and text stay SDR in look. This plan is the colour model
that lets any colour in the library be brighter than white or more saturated
than sRGB, and the renderer choice that lets one renderer show it while the
others clip it. The advanced renderer uses Vulkan on both platforms.

Status: under discussion. Nothing is built. This is a green-field change:
goldens that move are regenerated, not worked around.

Transfer functions such as PQ are output encodings the presenter picks; they
are not part of the model.

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
   SDR one": the advanced renderer shows SDR displays too.

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
changing the signature. The advanced renderer's own output stage (HDR out, or
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
a clipped, pale red cell; the advanced renderer sees a value of 6 and its
bloom pass lights the surroundings. No "emissive" attribute exists:
*emissive is a colour above 1*, and bloom's threshold is 1.

### Compositing: always in linear light

`BufferPlane::compose()` computes `bg_blend`, `fg_contrast`, backdrop dims
and tints, and every blend in `Color` in linear light, for every backend,
SDR included. One rule means one result: a blend that gave different answers
for a terminal and for the advanced renderer would make every effect depend on the
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
  and that stays for the basic renderer. The advanced renderer keeps the
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
- **`advanced`**: the GPU renderer, in Vulkan. A float framebuffer, the cells
  and glyph atlas, the lighting model (ambient occlusion, subsurface
  scattering, bloom) and an output stage. The output stage is **HDR when the
  display reports HDR** and tone-mapped SDR when it does not, so the effects
  look right on every display; HDR is its best output, not a requirement for
  it.

Why Vulkan: HDR reaches a display as a float or 10-bit image tagged with a
colour space, and OpenGL has no portable way to produce or tag one (a
`GtkGLArea` is composited as SDR). Vulkan renders to any image format, can
export it (as a dma-buf on Linux, a swap chain on Windows), and names the
colour space (`VK_EXT_swapchain_colorspace`).

At start, before any window is shown, the window backend probes and decides
for the run: `--renderer basic|advanced|auto`, `auto` by default.

```
auto: effects if
        - a Vulkan device can be created with the features the renderer
          needs (floor to be fixed when the shaders exist), it is a hardware
          device (not lavapipe or another CPU device), on Linux it can export
          images the host can import (below), and the driver is not on the
          known-bad list;
      else basic
```

- The terminal is always `basic`: it has no GPU.
- Nothing falls back during a frame or between frames. If the chosen renderer
  fails after the choice (device lost, shader error), that is an error
  reported and the program exits, not a silent change of look; `--renderer
  basic` is the user's escape. A CPU device fails the probe in advance, so a
  machine that would crawl is never given `advanced`.
- **What can still change at run time is the display, not the renderer**:
  HDR toggled in the OS, a window moved to another monitor. `advanced` has
  both outputs, so it re-reads the display's capabilities on the change event
  and rebuilds its swap chain between frames, inside the renderer. If
  startup-only is simpler to ship, the first version logs "restart to apply".
- **Blur.** The current blur (a GSK node, `term::BackdropEmulation`'s box
  passes) stays as it is for `basic` and the terminal; it blurs `Color`s
  clipped at the end, nothing else. `advanced` has its own separable blur on
  float textures, the same pass bloom uses (threshold at 1.0, downsample
  chain), so a blurred backdrop there keeps its highlights. The two are not
  made to look identical.
- **Pixel regions** (the scopes, canvas images) stay 8-bit sRGB: canvas stays
  SDR. In `advanced` they are sampled as sRGB textures, linearised and may be
  multiplied by a gain.
- **One GPU API, no abstraction layer.** The renderer talks to Vulkan
  through a thin layer of our own: a function loader (volk), a memory
  allocator (VMA), a handful of helpers for images, pipelines and frames in
  flight. Shaders are one language (GLSL or Slang) compiled to SPIR-V at build
  time and embedded. Direct3D is not a second back end; it stays a
  contingency if Vulkan proves unreliable on Windows drivers in the field
  (below), and the thin layer is the seam where it would go. SDL3's GPU API
  was considered and rejected: it wraps Vulkan and Direct3D 12 but exposes
  no external-memory export, which the Linux route below depends on.

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
- `advanced` output is checked offscreen on a CPU device (lavapipe on Linux,
  WARP on Windows, forced past the probe by a test-only flag), float read
  back, with a tolerance, in the gallery's golden style. CI cannot show HDR,
  only check the values.

## GTK: staying, with a dma-buf texture

GTK stays: the menu bar, dialogs, file choosers, input methods, the native
text inputs and the accessibility tree are what it is there for, and the
`advanced` renderer needs only to hand it a picture.

GTK has `GtkGLArea` and no Vulkan equivalent, so the `advanced` renderer
**renders offscreen in Vulkan and gives GTK the image as a
`GdkDmabufTexture`** (GTK 4.14 and later, zero copy on Linux). The widget
shows that texture; GTK composites it with its own colour state, which is how
HDR reaches the compositor once GTK's support settles (the groundwork landed
in 4.16: `GdkColorState`, and a colour state on the dma-buf texture builder).
(An earlier draft of this plan said the only route was a native child
surface of ours; that was wrong, and it is now the contingency.)

- **The frame:** the Vulkan image is allocated exportable
  (`VK_EXT_external_memory_dma_buf`, with a DRM format modifier GTK can
  import), drawn into, and wrapped per frame in a texture built with
  `GdkDmabufTextureBuilder`: FP16 (`ABGR16161616F`) when the display is HDR,
  10-bit or 8-bit otherwise, with the colour state set (linear Rec.2020, or
  PQ for HDR10). A small ring of such images is in flight; the texture's
  destroy notify returns the image to the ring once GTK is done with it, and
  the draw is fenced before the texture is handed over.
- **Graphics offload:** wrapping the widget in `GtkGraphicsOffload` lets GTK
  put the texture on its own Wayland subsurface instead of composing it,
  which is the cheapest path to scanout and to a compositor that does colour
  management. It is an optimisation to try, not a dependency.
- **The probe checks the host in advance:** the dma-buf formats and modifiers
  GDK reports for the display (`gdk_display_get_dmabuf_formats()`) must
  intersect what our Vulkan device can export; on a hybrid-GPU machine the
  device chosen is the one GTK renders on. No FP16 format in common: `advanced`
  runs with a 10-bit or 8-bit target and SDR output. No common format at all,
  or GTK older than 4.14: `basic`.
- **What is unknown, to be measured on a Wayland HDR setup before building on
  it:** whether the GTK and compositor of the day pass an FP16 or PQ dma-buf
  through as HDR or tone-map it to SDR; whether the colour state on the
  texture reaches the compositor at all. If it does not, Linux has the
  `advanced` renderer with SDR output until it does, which is the
  design's normal case anyway. X11 has no HDR.
- **Input and layout** are unchanged: the texture is a widget, so size, scale,
  focus, popups, native text inputs and the pointer are GTK's, as they are for
  `basic`; effects do not reach GTK's own popups.
- **Contingency:** an SDL3 host instead of GTK (next section). A
  `wl_subsurface` of ours inside the GTK window is the lesser alternative.

Windows has no host question: the Win32 window owns its Vulkan swap chain.

## SDL3 as the only host (the option if the texture route cannot carry HDR)

Dropping GTK for SDL3 is possible; this is what it covers and costs. The
renderer, `Color` and the thin Vulkan layer are the same either way, so the
choice is only the window.

- **Covered by SDL3:** windows, input, text input with IME (events and a
  candidate-window area to place), clipboard, drag and drop, HiDPI, cursors,
  a Vulkan surface (`SDL_Vulkan_CreateSurface`), and HDR window properties
  and a change event (HDR on, SDR white level, headroom), which would replace
  the display queries of the Windows plan on both platforms. Wayland
  colour-management support in SDL and in Mesa's Vulkan WSI is recent: check
  the versions before relying on it.
- **File picker: covered.** `SDL_ShowOpenFileDialog`, `SDL_ShowSaveFileDialog`
  and `SDL_ShowOpenFolderDialog` are native: Windows' own dialogs, and on
  Linux the desktop portal (with a fallback to a helper program where there is
  no portal). Simple message boxes too (`SDL_ShowSimpleMessageBox`). They are
  asynchronous (a callback), which the existing dialog code must be shaped
  for; same as the portal-backed GTK ones.
- **Lost:** native and native-looking menus (the menu bar is drawn by Cell
  UI on both platforms, popup windows), native text fields (Cell UI's own
  `InlineEditor`/`LineEditor` already draw them, with the IME events), the
  accessibility tree on Linux (SDL has none; AT-SPI would be ours to write),
  and on Windows a UI Automation provider would have to hook the window
  through SDL's message hook. The accessibility step in `plans/cellui-
  window.md` gets bigger, not different.
- **Gained:** one window backend for Linux and Windows instead of GTK plus
  Win32, and a plain Vulkan swap chain on Linux: no dma-buf export, no
  dependence on GTK's colour state. The packaging is vendored SDL3 (Ubuntu
  24.04 has only SDL2).
- **When:** after the Vulkan spike. If the GTK texture reaches the compositor
  as HDR, GTK stays on Linux. If it does not, SDL3 becomes the host for the
  `advanced` renderer on Linux; and whether SDL3 should then also replace the
  planned Win32 backend (one backend, against native menus and UI Automation,
  which the Windows plan values) is decided separately, on the Windows
  priorities.

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
6. **Vulkan spike**: the thin layer, one shading language to SPIR-V, an FP16
   target; on Linux exported as a dma-buf and shown in a GTK window as a
   `GdkDmabufTexture` (formats, modifiers, fencing, the ring); on Windows
   presented through a Vulkan swap chain in HDR10 and in scRGB on a real HDR
   monitor. Decides whether Vulkan alone is enough on Windows.
7. **`advanced` renderer, tone-mapped SDR out**: cell quads, glyph atlas,
   float framebuffer, the shared blur. The gallery renders through it
   offscreen on a CPU device.
8. **Linux HDR output**: whether the texture carries HDR to the compositor
   (above), on a Wayland HDR setup; if not, an SDL3 spike (swap chain, HDR
   properties, file dialogs, IME) on the same machine.
9. **Windows HDR output** (`plans/cellui-window.md`): needs the Win32
   backend first.
10. **Effects** (`plans/cellui-effects.md`): bloom first (wide colours and
   the blur), then ambient occlusion and subsurface scattering, which need
   the cube model's effect attributes (height or normal and a thickness per
   cell, set through the plane API).
11. **Scopes**: a plus, last. Their 8-bit canvas images stay; in `advanced` a gain
    on the image lights peaks and the clip red above 1 (`Image::gain`, applied
    by the sampler). Float images from `SpectrumImage`/`HeatmapImage`/
    `VuMeterImage` (hand-drawn already) only if 8 bits show banding in the
    heatmap's dark end.
12. **Theme**: wide variants for the tokens that glow (playhead, the LED hues
    shared with the Launchpad, the clip pulse's peak), written in the
    notation above.

## Open questions

- The real `toSrgb8()` mapping (curve, gamut mapping), and whether the same
  curve is the `advanced` output stage on an SDR display: from the
  tone-mapping work. Only the signature (a pure function of one `Color`, no
  state) is needed in advance; the placeholder is enough until the gallery
  swatch page is looked at by eye. It does not block steps 1 to 5.
- Whether Vulkan alone is reliable enough on Windows drivers (Intel in
  particular) for HDR swap chains; the contingency is Direct3D 12 behind the
  same thin layer. Decided by the Vulkan spike on real hardware.
- The Vulkan floor (1.2 or 1.3) and the required extensions: with the shaders.
- Whether `advanced` follows display changes at run time or the first version
  applies them at restart.
