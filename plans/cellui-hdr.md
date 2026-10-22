# cellui: an HDR and wide-gamut colour model

Effects (`plans/cellui-effects.md`) are where HDR comes from: bloom around an
LED, light scattered through a translucent slab, ambient occlusion in a
crease. Flat cells and text stay SDR in look. This plan is the colour model
that lets any colour in the library be brighter than white or more saturated
than sRGB, and the backend choice that lets one backend show it while the
others clip it. That backend renders with Vulkan on both platforms.

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
4. **The backend is chosen once, at start, from what the machine can do.**
   No switching, no fallback inside a frame (below). Backends differ in what
   they can draw, not in a tier: the terminal and the toolkit windows draw
   SDR only; the Vulkan backend draws SDR and HDR.

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
changing the signature. The Vulkan backend's own output stage (HDR out, or
tone-mapped for an SDR display) is a function of the same family, in the
backend, not here. In-gamut colours with every component at most 1 must map
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
`bg = Color("oklch(0.8 0.2 25)") * 6`: the terminal and the toolkit windows show
a clipped, pale red cell; the Vulkan backend sees a value of 6 and its
bloom pass lights the surroundings. No "emissive" attribute exists:
*emissive is a colour above 1*, and bloom's threshold is 1.

### Compositing: always in linear light

`BufferPlane::compose()` computes `bg_blend`, `fg_contrast`, backdrop dims
and tints, and every blend in `Color` in linear light, for every backend,
SDR included. One rule means one result: a blend that gave different answers
for a terminal and for the Vulkan backend would make every effect depend on the
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
  and that stays for the toolkit backends. The Vulkan backend keeps the
  coverage mask in its glyph atlas and mixes in linear light, which makes
  light-on-dark text look bolder and dark-on-light thinner than the gamma mix
  does; a contrast adjustment on the coverage is tuned by eye if it shows.

Equality and hashing (the frame diff, backdrop tile fingerprints, the
window's texture keys) compare the floats bitwise, so equal compositions give
equal colours; the composer is deterministic, so that is enough.

## Choosing the backend

cellui has one switch for what draws the UI: `--ui auto|terminal|gtk|vulkan`
(on Windows `win32` joins them). It names a `cellui::Backend`
implementation; there is no separate renderer switch and no "basic" or
"advanced" tier. `window` is retired in favour of the toolkit's name.

- **`terminal`, `gtk` (and `win32`)** happen to draw SDR only. They take every
  `Color` through `toSrgb8()`, ignore effect attributes and know nothing of
  HDR.
- **`vulkan`** renders both SDR and HDR: a float framebuffer, the cells and
  glyph atlas, the lighting model (ambient occlusion, subsurface scattering,
  bloom) and an output stage that is **HDR when the display reports HDR** and
  tone-mapped SDR when it does not, so the effects look right on every
  display. A Direct3D backend with the same role may follow if Vulkan proves
  unreliable on Windows drivers; it would be one more name for `--ui`, not a
  mode.
- **`auto`** is the default and picks the best backend the system can handle,
  by probing **in advance**, before anything is shown, in this order:
  `vulkan`, then the toolkit window (`gtk`, `win32`), then `terminal`.

```
vulkan  if - a display is available, and
           - a Vulkan device can be created with the features the backend
             needs (floor to be fixed when the shaders exist), and
           - it is a hardware device (not lavapipe or another CPU device), and
           - the driver is not on the known-bad list
window  if a display is available and the toolkit is built in
terminal otherwise
```

- An explicit `--ui X` that cannot start is an error, not a fallback; only
  `auto` falls through, and only at start. A CPU Vulkan device fails the probe,
  so a machine that would crawl is never given `vulkan` by `auto`.
- Nothing is switched or fallen back to during a run, inside a frame or
  between frames. If the chosen backend fails after it has started (device
  lost, shader error), that is an error reported and the program exits.
- **What can still change at run time is the display, not the backend**: HDR
  toggled in the OS, a window moved to another monitor. `vulkan` has both
  outputs, so it re-reads the display's capabilities on the change event and
  rebuilds its swap chain between frames, inside the backend. If startup-only
  is simpler to ship, the first version logs "restart to apply".
- **Blur.** The toolkit windows' blur (a GSK node) and the terminal's
  (`term::BackdropEmulation`'s box passes) stay as they are; they blur `Color`s
  clipped at the end, nothing else. `vulkan` has its own separable blur on
  float textures, the same pass bloom uses (threshold at 1.0, downsample
  chain), so a blurred backdrop there keeps its highlights. The two are not
  made to look identical.
- **Pixel regions** (the scopes, canvas images) stay 8-bit sRGB: canvas stays
  SDR. In `vulkan` they are sampled as sRGB textures, linearised and may be
  multiplied by a gain.
- **One GPU API, no abstraction layer.** The backend talks to Vulkan through a
  thin layer of our own: a function loader (volk), a memory allocator (VMA),
  a handful of helpers for images, pipelines and frames in flight. Shaders are
  one language (GLSL or Slang) compiled to SPIR-V at build time and embedded.
  Direct3D stays a contingency, and the thin layer is the seam where it would
  go. SDL3's GPU API was considered and rejected: it wraps Vulkan and Direct3D
  12 but we want the full Vulkan feature set and swap-chain colour-space
  control.

Why Vulkan: HDR reaches a display as a float or 10-bit image tagged with a
colour space, and OpenGL has no portable way to produce or tag one. Vulkan
renders to any image format and names the colour space of its swap chain
(`VK_EXT_swapchain_colorspace`).

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

The terminal and the toolkit windows leave it unset. `peak` is relative so an effect can
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
- `vulkan` output is checked offscreen on a CPU device (lavapipe on Linux,
  WARP on Windows, forced past the probe by a test-only flag), float read
  back, with a tolerance, in the gallery's golden style. CI cannot show HDR,
  only check the values.

## The vulkan backend's window: SDL3

The `vulkan` backend is a window of its own, not a texture inside the GTK
window (a `GdkDmabufTexture` route was considered and dropped: it would make
`gtk` an HDR backend, and the model is that `gtk` draws SDR only). The window
is **SDL3**, on Linux and Windows alike, so the backend is one piece of code
on both.

- **Covered by SDL3:** windows, input, text input with IME (events and a
  candidate-window area to place), clipboard, drag and drop, HiDPI, cursors,
  a Vulkan surface (`SDL_Vulkan_CreateSurface`), and HDR window properties and
  a change event (HDR on, SDL white level, headroom), which feed
  `DisplayInfo::hdr` on both platforms. Wayland colour-management support in
  SDL and in Mesa's Vulkan WSI is recent: check the versions before relying on
  it; where it is missing the backend runs with SDR output (X11 has no HDR).
- **File picker: covered.** `SDL_ShowOpenFileDialog`, `SDL_ShowSaveFileDialog`
  and `SDL_ShowOpenFolderDialog` are native: Windows' own dialogs, and on
  Linux the desktop portal (with a fallback to a helper program where there is
  no portal). Simple message boxes too (`SDL_ShowSimpleMessageBox`). They are
  asynchronous (a callback), which the existing dialog code must be shaped for.
- **Lost against the toolkit backends:** native and native-looking menus (the
  menu bar is drawn by cellui on this backend, as popup windows), native text
  fields (`InlineEditor`/`LineEditor` already draw them, with the IME events),
  the accessibility tree on Linux (SDL has none; AT-SPI would be ours to
  write), and on Windows a UI Automation provider would have to hook the
  window through SDL's message hook. The accessibility step in
  `plans/cellui-window.md` gets bigger on this backend, not different.
- **Kept for those who want them:** `--ui gtk` (and `win32`) stay as the
  native-toolkit windows, SDR only. Since `auto` prefers `vulkan`, a user who
  values native menus and accessibility over effects names `gtk`; a saved
  setting for the default is a small addition.
- **Packaging:** vendored SDL3 (Ubuntu 24.04 has only SDL2).
- **Windows:** `vulkan` presents through a Vulkan swap chain on the SDL3
  window. Whether the planned `win32` backend (Direct2D, native menus, UI
  Automation) is still built is decided separately, on the Windows
  priorities in `plans/cellui-window.md`.

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
5. **Backend choice**: `--ui auto|terminal|gtk` with the probe and `auto` as
   the default (`window` retired), the probe's tests (no display, CPU device,
   bad driver); `vulkan` does not exist yet, so the seam is proved.
6. **Vulkan spike**: the thin layer, one shading language to SPIR-V, an FP16
   target; an SDL3 window with a Vulkan swap chain on Linux and on Windows in
   SDR first; the SDL3 file dialogs and IME; then HDR10 and scRGB on a real HDR
   monitor on Windows. Decides whether Vulkan alone is enough on Windows.
7. **`vulkan` backend, tone-mapped SDR out**: cell quads, glyph atlas, float
   framebuffer, the shared blur, as a `cellui::Backend`; `--ui vulkan`, and
   `auto` starts picking it. The gallery renders through it offscreen on a CPU
   device.
8. **Linux HDR output**: SDL3 and Mesa colour management on a Wayland HDR
   setup; if it is not there yet, `vulkan` keeps SDR output on Linux.
9. **Windows HDR output** (`plans/cellui-window.md`): needs the Win32
   backend first.
10. **Effects** (`plans/cellui-effects.md`): bloom first (wide colours and
   the blur), then ambient occlusion and subsurface scattering, which need
   the cube model's effect attributes (height or normal and a thickness per
   cell, set through the plane API).
11. **Scopes**: a plus, last. Their 8-bit canvas images stay; in `vulkan` a gain
    on the image lights peaks and the clip red above 1 (`Image::gain`, applied
    by the sampler). Float images from `SpectrumImage`/`HeatmapImage`/
    `VuMeterImage` (hand-drawn already) only if 8 bits show banding in the
    heatmap's dark end.
12. **Theme**: wide variants for the tokens that glow (playhead, the LED hues
    shared with the Launchpad, the clip pulse's peak), written in the
    notation above.

## Open questions

- The real `toSrgb8()` mapping (curve, gamut mapping), and whether the same
  curve is the `vulkan` output stage on an SDR display: from the
  tone-mapping work. Only the signature (a pure function of one `Color`, no
  state) is needed in advance; the placeholder is enough until the gallery
  swatch page is looked at by eye. It does not block steps 1 to 5.
- Whether Vulkan alone is reliable enough on Windows drivers (Intel in
  particular) for HDR swap chains; the contingency is a Direct3D backend
  behind the same thin layer. Decided by the Vulkan spike on real hardware.
- Whether `auto` should prefer `vulkan` over the toolkit window when the user
  has no use for effects (native menus and accessibility are what is given
  up): a saved default, or `auto` stays as it is.
- The Vulkan floor (1.2 or 1.3) and the required extensions: with the shaders.
- Whether `vulkan` follows display changes at run time or the first version
  applies them at restart.
