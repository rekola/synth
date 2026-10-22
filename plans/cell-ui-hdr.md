# Cell UI: an HDR and wide-gamut colour model

Effects (`plans/cell-ui-effects.md`) are where HDR comes from: bloom around an
LED, light scattered through a translucent slab, ambient occlusion in a
crease. Flat cells and text stay SDR. This plan is the colour model that lets
a cell say "brighter than white" or "more saturated than sRGB", and the
renderer split that lets one backend show it while every other path ignores
it.

Status: under discussion. Nothing is built.

"HCG" in the request is read as **WCG**, wide colour gamut (Display-P3,
Rec.2020). If something else was meant (HLG, the transfer function, is the
other candidate), the model below holds: HLG and PQ are *output encodings*
chosen by the presenter, not part of the model.

## Principles

1. **The SDR colour is always there.** A cell with a wide colour still has
   its ordinary `fg`/`bg`, filled in when the wide colour is set: its SDR
   rendition. The terminal, the current renderer, `FrameEncoder`,
   `toAnsi()`, the scopes' fallbacks and every existing test read only that,
   so none of them knows HDR exists. Not showing HDR is never an error; it is
   the default.
2. **HDR is for the renderer that can show it.** An OpenGL renderer reads the
   wide colours and the effect attributes; anything else (no GL, a software
   rasteriser, a remote session, a display that is not HDR and a user who
   asked for the plain look, a lost context) falls back to the current
   renderer, which draws the renditions. The fallback is chosen at run time
   and can change while the window is open.
3. **Values are relative to SDR white.** 1.0 is the display's SDR reference
   white, whatever its nits; 4.0 is four times as much light. The UI never
   speaks in nits (the user's SDR white slider decides what 1.0 is, 80 to 400
   nits on Windows); the presenter turns relative into absolute.
4. **No existing pixel changes.** A cell with no wide colour composes
   exactly as today, byte for byte, so the goldens stay as they are.

## The model

### `cellui::WideColor` (`cellui/WideColor.h`)

Three floats: **linear light, Rec.709 primaries, extended range, 1.0 = SDR
white**. This is scRGB: it is what a Windows HDR swap chain takes natively
(times 80 nits / the user's white level), what an FP16 OpenGL framebuffer
holds, and a straight matrix away from Display-P3 and Rec.2020. Values above
1 are HDR; negative components are colours outside sRGB's gamut (a P3 red is
`R > 1`, `G < 0`). The alternative, linear Rec.2020 with no negatives, was
rejected: every sRGB colour in the theme would then be a matrix product away
from the numbers a designer wrote, and the swap chain needs scRGB anyway.

```
WideColor fromSrgb(const Color &, float gain = 1);        // the theme's colours, optionally lit
WideColor fromLinear(float r, float g, float b);          // scRGB itself
WideColor fromDisplayP3(float r, float g, float b, float gain = 1);   // gamma-encoded, as written
WideColor fromRec2020(float r, float g, float b, float gain = 1);
WideColor fromOklch(float l, float c, float h, float gain = 1);       // l 0..1 as now; gain lights it
Color     toColor() const;                    // the SDR rendition, below
float     luminance() const;                  // Rec.709 Y, relative to SDR white
WideColor mix(const WideColor &, float t) const;   // in linear light
```

`Color` is unchanged. A `WideColor` is the rich type; a `Color` is what an
SDR path takes.

### The SDR rendition (`toColor()`)

Tone and gamut mapping, in one place so every fallback agrees:

1. Gamut: a colour outside sRGB is pulled in along constant OKLCH hue and
   lightness (reduce chroma until it fits), not clipped per channel, which
   shifts hue.
2. Range: for a maximum component above 1, scale the colour into range and
   bleed the excess toward white, the way bloom would have: `c' = c/m`, then
   `mix(c', white, 1 - 1/m)` for `m = max(c)`. A 4x red LED is then a bright,
   slightly pale red, and a 40x one white. The curve is a function in one
   file with a golden table; its exact shape is judged by eye on the gallery.
3. Encode with the sRGB transfer to 8 bits.

### Notation

Three places want a notation: source (a theme file, the gallery, tests), the
terminal (never; it gets the rendition) and the API.

**Text**, parsed by `WideColor(std::string_view)` and used by `Theme` entries
and tests. It is CSS Color 4's functional notation where that has an
equivalent, plus one suffix for light:

| Written | Meaning |
|---|---|
| `#ff8040` | sRGB, SDR: what `Color` takes now |
| `oklch(0.75 0.15 60)` | as `Color::fromOklch()` |
| `color(srgb-linear 2 0.5 0.1)` | scRGB itself; above 1 is HDR |
| `color(display-p3 1 0.2 0.1)` | gamma-encoded P3, as written in CSS |
| `color(rec2020 0.8 0.1 0.1)` | gamma-encoded Rec.2020 |
| *any of the above* `*4` | multiplies the linear light by 4 ("four times SDR white"); `#ff8040*4`, `oklch(0.8 0.2 25)*3` |

`*N` is the HDR notation proper. It is relative (principle 3), reads as
"this colour, N times brighter", works on any base colour, and degrades by
design: strip the suffix and what is left is the SDR colour. CSS's own HDR
colour spaces (`rec2100-pq`, `rec2100-hlg`) are absolute-luminance encodings
that need a display model to mean anything; they are accepted for *import*
only if an asset needs them, and never written by the UI. Nits are not
accepted at all.

**API.** A cell's wide colours are set beside the SDR ones, on the plane:

```
plane->setFgWide(WideColor);   // sets fg to the rendition too
plane->setBgWide(WideColor);
plane->setFgColor(r,g,b);      // as now; also clears the cell's wide fg
```

so a widget that never calls them is unchanged, and one that does gets the
fallback for free. `CellBuffer::Cell` and `Style` gain
`std::optional<WideColor> fg_wide, bg_wide` (set only while a wide colour
was asked for; unset means "the SDR `fg`/`bg` is the whole story"). The
invariant, kept by the setters and tested: `fg_wide` set implies
`fg == fg_wide->toColor()`.

**LED example.** The LED in the middle of a volumetric button is a cell
whose background is `oklch(0.8 0.2 25)*6`: the SDR path shows a pale red
cell; the GL renderer sees a value of 6 and its bloom pass lights the
surroundings. No separate "emissive" attribute exists: *emissive is a wide
colour above 1*, and the bloom threshold is 1.

### Compositing

`BufferPlane::compose()` resolves `bg_blend`, `fg_contrast` and backdrops by
averaging and leaning towards white or black, all on 8-bit gamma values
today. With any operand wide, it computes in `WideColor` (blends in linear
light, which is also the correct way and will not match the SDR blend of a
non-wide pair, deliberately), and stores both the wide result and its
rendition. With no wide operand it takes today's code path unchanged.
Everything that fingerprints or compares cells (the frame diff, the
backdrop tiles' fingerprints in `term::BackdropEmulation`, the window's
texture keys) hashes the wide fields too, or a changed LED would not be
redrawn.

`Backdrop::tint`, `Band::color` and the theme's `cursor_tint` stay `Color`.
A wide tint is added when an effect needs one.

### Display capabilities

`DisplayInfo` gains what the renderer split needs:

```
struct HdrInfo {
  bool available = false;       // the display shows HDR right now
  float peak = 1;               // peak luminance, in SDR whites (nits / sdr_white_nits)
  float sdr_white_nits = 0;     // 0 unknown
  Gamut gamut = Gamut::SRGB;    // SRGB, DISPLAY_P3, REC2020: the display's primaries
};
std::optional<HdrInfo> hdr;     // unset: the backend has no HDR path at all
```

`peak` is relative so effects can scale to it (a 4x LED on a 1.5x-peak
display is just clipped). It changes while the window is open (HDR toggled
in the OS, the window moved to another monitor), so the application gets a
`BackendEvent` for it, as for a resize.

## The renderer split

```
 cells + attributes (CellBuffer, planes, effects)
          |
   FrameInfo (cells, wide colours, effect attributes, backdrops)
          |
   +------+---------------------------+
   | Backend::present()               |
   |  SDR renderer (today)            |  reads fg/bg only; GSK/Direct2D/terminal
   |  GL renderer (new, per window)   |  reads everything; FP16; HDR or tone-mapped out
   +----------------------------------+
```

- The GL renderer is a second implementation behind the window backend's
  existing present step, not a new `Backend`. It owns an FP16 framebuffer,
  draws the cells (the same glyph atlas and quads the plan's SDL_GPU section
  describes), runs the effects (ambient occlusion, subsurface, bloom) in
  float, and ends in an *output stage*: scRGB or HDR10 where the display is
  HDR, a tone-map to SDR where it is not. So the effects work on SDR
  displays as well; HDR is one output of the renderer, not a precondition.
- **Fallback is per frame and silent**: no GL context, GL older than 3.3 (or
  ES 3.0), a software rasteriser (llvmpipe, WARP-style) detected by renderer
  string, context loss, a frame that took more than its budget twice in a
  row, `--renderer sdr`. The window logs why once and shows the SDR
  renderer's frame; nothing about the model changes, since the renditions
  are already in the cells.
- **Blur.** The current blur (a GSK blur node, `term::BackdropEmulation`'s
  box passes) is 8-bit and stays that way. The GL renderer has its own
  separable blur on float textures, the same pass bloom needs (a threshold
  at 1.0, then a downsample chain). A modal's backdrop in the GL renderer is
  therefore blurred in float and keeps its highlights; in the fallback it is
  blurred as today. No attempt is made to make the two look identical, only
  to make the fallback look right.
- **Pixel regions** (the scopes, canvas images) stay 8-bit sRGB textures in
  both: canvas stays SDR. In the GL renderer they are sampled and
  linearised like anything else, and may be multiplied by a gain.

## The test backend

`TestBackend` already keeps the composed frame and `FrameInfo` so tests read
what the UI showed. For HDR:

- `frame()` cells carry the wide fields, so `fg_wide`/`bg_wide` assertions
  read what composition did (`bg_blend` of a wide cell over an SDR one, a
  backdrop over a lit LED).
- `info().hdr` is settable: `setHdr(HdrInfo)` and `setHdr(std::nullopt)`,
  emitting the display-change event, so a test checks that the application
  reacts to HDR appearing and going away (and that an app with no HDR
  display never sets a wide colour that has no rendition).
- `emulateHdr(bool)`, as `emulateBackdrops()`: when on, `frame()` is the
  *rendition* (what an SDR renderer draws) and `wideFrame()` the wide
  values, so one test can assert both halves of the invariant. Default off:
  it returns the cells as composed.
- Pure functions get unit tests with no backend: `WideColor` parsing (every
  row of the notation table, rejects nits), matrices round trips (P3 and
  Rec.2020 to scRGB and back within 1e-4), `toColor()` against a golden
  table (an sRGB colour is returned exactly; `*1` of any sRGB colour is the
  identity), the compose rule (no wide operand = old result).
- GL output (later) is checked offscreen against a software GL (llvmpipe),
  FP16 read back, with a tolerance, in the gallery's golden style.

## Should GTK go?

Not now, and the way to find out is a spike, not an opinion.

**What GTK gives**: the menu bar, dialogs, file choosers, input methods,
IME and the accessibility tree (AT-SPI) on Linux; everything
`plans/cell-ui-window.md` chose it for. Replacing it means writing those
ourselves or taking SDL3, which has none of them either (that is why it was
not taken).

**What it does not give yet**: Ubuntu 24.04's GTK is 4.14, the oldest this
supports, and has no colour-state or float-texture API worth the name for
this (`GdkColorState` and FP16 memory textures arrived in 4.16; the
compositor-facing side, Wayland's colour-management protocol, later again
and only where the compositor has it). X11 has no HDR at all. So **HDR on
Linux is a newer-distro, Wayland feature** whatever toolkit sits under it;
the fallback renderer is what everyone else sees, and the design above
already has that as the normal case.

**What the HDR renderer needs from the host** is only: a GL (or GLES)
context in a surface, an FP16 or 10-bit format, and a way to tell the
compositor the surface is scRGB/PQ. `GtkGLArea` gives the first, and the
rest depends on the GTK and compositor of the day. Spike, on a Wayland HDR
setup (a current GNOME or KDE), in this order:

1. `GtkGLArea` with an FP16 framebuffer: does GTK 4.16+ composite it as
   HDR, or tone-map it to SDR? Is there a way to give the area a colour
   state?
2. If not, a Wayland subsurface of ours under the GTK window, with a GL
   context and `wp_color_management_v1` set directly (the surface is ours;
   GTK need only position it). Works only on Wayland, and is the main
   cost.
3. If both fail on the GTK versions in use: the HDR renderer is **disabled
   on Linux** until they do (a one-line capability), not a reason to
   change toolkit.

Switch away only if the spike shows the GTK window *and* the subsurface
route are both unworkable **and** Linux HDR is a goal worth the loss of
GTK's menus, dialogs and accessibility; then it is SDL3 with the GL
renderer (SDL3 exposes HDR window properties and float swap chains) and
Cell UI's own menus and dialogs, a large step, taken with eyes open. The
renderer split above makes that a swap of the host, not of the model or the
effects.

## Steps

1. **`WideColor`**: type, conversions, `toColor()`, text notation, with the
   unit tests above. No other file changes. Gallery gets a swatch page that
   shows a colour next to its rendition.
2. **Cells**: `fg_wide`/`bg_wide` on `Cell` and `Style`, `setFgWide()`/
   `setBgWide()` on `UIPlane`/`BufferPlane`, the invariant, hashing and
   equality, `describe()` output. Everything reads renditions; run the whole
   suite, no golden changes. **This is the first step asked for**: setting a
   cell's foreground and background in HDR.
3. **Compose in wide** (`BufferPlane`): `bg_blend`, `fg_contrast`,
   backdrops, lift, scroll layers. Tests for each against the rule.
4. **`DisplayInfo::hdr`** and the display-change event; `TestBackend`
   `setHdr()`/`emulateHdr()`/`wideFrame()`; terminal and GTK report "none".
5. **Renderer split in the window backend**: an interface behind the present
   step with the SDR renderer as the only implementation and the fallback
   logic (reasons, logging, `--renderer sdr|gl|auto`) in place. No GL yet;
   proves the seam.
6. **GL renderer, SDR out**: cell quads, glyph atlas, float framebuffer,
   tone-mapped output, plus the shared blur. The gallery renders through it
   offscreen against a software GL.
7. **Host spike on Linux** (above), then HDR output there if it passes.
8. **Windows HDR output** (`plans/cell-ui-window.md`): needs the Win32
   backend first.
9. **Effects** (`plans/cell-ui-effects.md`): bloom first (it needs only wide
   colours and the blur), then ambient occlusion and subsurface scattering,
   which need the effect attributes of the cube model (height or normal and
   a thickness per cell, set by the same plane API).
10. **Scopes**: a plus, after the rest. The spectrum, the heatmap and the VU
    meters keep their 8-bit canvas images; in the GL renderer a gain on the
    image lights peaks and the clip red above 1 (a per-image
    `Image::gain`, applied by the sampler). Producing float images (the
    `SpectrumImage`, `HeatmapImage` and `VuMeterImage` renderers are already
    hand-written and could emit FP16 directly) comes only if 8 bits show
    banding in the dark ends of the heatmap's ramp.
11. **Theme**: wide variants for the few tokens that glow (playhead, LED
    hues shared with the Launchpad, clip-pulse peak), written in the
    notation above, with the SDR hex kept as the base.

## Open questions

- The rendition's range curve (step 1): bleed-to-white or a filmic shoulder;
  decided by looking at the gallery page, then frozen in the golden table.
- Whether `Backdrop::tint` and `Band` need wide forms in step 3 or wait for
  an effect that wants one.
- A per-window user setting for "HDR peak" when the OS reports none (a lot
  of Linux setups do not).
