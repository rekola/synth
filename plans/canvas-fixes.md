# canvas fixes

Work in `rekola/canvas` (default branch `master`) before and while synth
starts drawing with it (`plans/cell-ui-terminal.md`, "2D drawing: canvas").
canvas has other users, so nothing is removed that anyone could still use:
every backend, the texture packing (DXT, ETC1, `PackedImageData`),
`FloydSteinberg`, `ColorProvider`, `HitRegion` and `PerlinSurface` stay. Each step is one pull
request with pixel tests for what it fixes, and each keeps the existing API
working unless it says otherwise.

## Already on master

- CMake build, one option per backend, exported `canvas::canvas` (#92).
- HTML5 geometry in the Cairo backend: integer coordinates are pixel edges
  (#92).
- Gaussian blur: symmetric kernel, transparent edges, safe on premultiplied
  data; Linux CI (Cairo) and Windows CI (GDI+, MSYS2 UCRT64, compile only)
  (#93).
- Texture packing optional (`CANVAS_TEXTURE_PACKING`, off by default) (#94).
- Unique include guards (`_CANVAS_` prefix), with a test that includes
  canvas after a client's own `Color.h`/`Image.h` (#95).

## Kept, and why

- **`FilenameConverter`**: the Quartz backend's hook for turning an image
  name into a path (an app bundle's resource path). Cairo and GDI+ have no
  hook and prefix `"assets/"` instead (step 3), so it becomes every
  backend's hook rather than going away.
- **`FloydSteinberg`**: error-diffusion dithering, today only to the 16-bit
  texture formats (RGBA4, RGB565, RGB555, RGBA5551) for `PackedImageData`.
  It does not reduce to a palette, so it doesn't serve a 256-colour terminal
  or Sixel yet; step 7 adds that.
- **`PerlinSurface`**: a read-only Perlin-noise surface, a source for
  `drawImage()`. It doesn't compile (item 25); fixing it is two small
  changes, so it is fixed rather than deleted.

## Bugs

Verified with small programs against master unless marked "by reading".

API and state (`Context`, `GraphicsState`, `Style`, `Attribute`):

1. **Gradients share one object.** `createLinearGradient()` returns a
   reference to a single `Style` member and never clears its stops, so the
   second gradient keeps the first one's stops (3 stops where 2 were set),
   and only copying each into `fillStyle` before making the next is safe.
   Return a new `Style` by value each call. This changes the API: a caller
   that writes `auto & g = ctx.createLinearGradient(...)` stops compiling
   (a reference can't bind to the returned value) and becomes `auto g`,
   so nothing changes behaviour silently.
2. **`save()`/`restore()` restore the path.** `currentPath` and `clipPath`
   live in `GraphicsState`; in HTML5 the current path is not drawing state
   (a rect added between save and restore disappeared). The clip region
   is drawing state and should keep being restored.
3. **Path points ignore the transform at the time they are added.** Points
   are stored untransformed and the transform at `fill()`/`stroke()` time
   is applied to all of them; HTML5 transforms each point when it is
   added. A path built across a `translate()` filled the wrong area.
4. **Line width, dashes and radii use only the matrix's `a`.**
   `TransformationMatrix::transformSize()` returns `s * a` (or
   `s * sqrt(a)`), so after `rotate(PI/2)` a stroke has width 0 and draws
   nothing, and a negative x scale gives a negative width. Use
   `sqrt(|ad - bc|)`.
5. **Arcs ignore rotation.** `Path2D::transform()` moves an arc's centre
   and radius but not its start and end angles (the angle code is
   `#if 0`). With step 3 points are transformed on entry; arcs need their
   angles (and a mirrored transform's direction) transformed too, or to
   be flattened when the transform isn't a uniform scale plus translation.
6. **`clip()` is wrong three ways.** It stores the path without the
   current transform (a clip set under `translate(8, 8)` clipped at the
   origin), it replaces the clip instead of intersecting with it, and it
   clears the current path, which HTML5 doesn't.
7. **`closePath()` jumps to the path's first point** instead of the start
   of the current subpath (after `rect(); rect(); moveTo(0, 12) ...` it
   moved to 0,0).
8. **Fluent setters return `GraphicsState&`.** `strokeStyle("#f00")`,
   `lineWidth(2)`, `translate()`, `clip()` and the rest return the base
   class, so the README's `strokeStyle("#ff0000").moveTo(...)` doesn't
   compile. Make them return `Context&`.
9. **`Style`/`Font` copy assignment copies the owner pointer** (implicit,
   deprecated `operator=` on `AttributeBase`): `a.fillStyle = b.fillStyle`
   across two contexts makes `a.fillStyle(...)` return `b`. Declare the
   assignments explicitly, copying the value only. This also removes the
   `-Wdeprecated-copy` warnings in the public headers.
10. **`isPointInPath()` always returns false.** Implement it with
    `Path2D::isInside()` after fixing that (item 11).
11. **`Path2D::isInside()`** closes every subpath back to the first point
    of the whole path, counts the jump of a `moveTo` as an edge, and
    ignores arcs. Flatten first, track subpath starts. (By reading.)
12. **`Path2D::getExtents()`** assigns `min_y = pc.x0`, `max_x = pc.y0`
    and compares with `<` for the maxima. (By reading.)
13. **`Color("rgba(...)")` asserts**, and `rgb(...)`, `#rrggbbaa` and named
    colours other than black and white aren't parsed. Parse `rgb()`,
    `rgba()`, `#rgba`/`#rrggbbaa` and the CSS named colours; an unknown
    string gives black, never an assert.
14. **`Image::isJPEG()`** accepts only the `FF D8 FF DB` marker, so JFIF
    (`E0`) and Exif (`E1`) files are rejected. Check `FF D8 FF`. (By
    reading.)

Pixels (`ImageData`, the backends):

15. **`ImageData` doesn't say what its bytes are.** `drawImage(ImageData)`
    copies 4-channel data in as the backend's native premultiplied BGRA,
    but treats 3-channel data as RGB, and `Image::loadFromFile()` gives
    straight-alpha RGBA. A loaded PNG with alpha is drawn with red and
    blue swapped and unpremultiplied. Give `ImageData` a format
    (channel order and straight/premultiplied), set it wherever an
    `ImageData` is produced, convert in `drawImage`, and keep
    `createImage()`'s output as it is (documented, and what synth's
    terminal output will read). The current pixel test that writes
    255 into byte 2 and reads red back is rewritten to state the format.
16. **1-channel Cairo surfaces ignore row stride.** Cairo pads A8 rows to a
    multiple of 4 bytes; `initializeSurfaceFromData()` and
    `CairoSurface::createImage()` assume none (a 5-px-wide row read back
    as `0 0 0 255 255`). Shadows use 1-channel surfaces, so shadows of
    most widths are sheared. Copy row by row with the stride.
17. **`ImageData` sizes overflow.** `calculateSize()` multiplies in
    `unsigned int`; `multiplyAlpha()` assumes 4 channels. Widen to
    `size_t`; return early for other channel counts.
18. **GDI+ gradients use only the first and last stop**, only for fills
    (a gradient stroke falls back to a solid pen), and tile beyond the end
    points where HTML5 pads. Use `SetInterpolationColors()` with every stop,
    extend the brush's rectangle past the path's extents with the end
    colours so nothing tiles, and a brush pen for strokes. (By reading.)
19. **GDI+ `drawImage` ignores `globalAlpha`** (`if (globalAlpha < 1.0f &&
    0)`). Use an `ImageAttributes` colour matrix. (By reading.)
20. **GDI+ 1-channel surfaces are `PixelFormat8bppIndexed`**, which a
    `Graphics` can't draw into, so shadows fail there. Use a 32bpp surface
    and extract alpha, as the shadow path already does on `WIN32`.
    (By reading.)
21. **Unhandled `Style::FILTER`** in the Cairo backend leaves the previous
    source set and draws with it. Draw nothing (or the solid colour) until
    filters exist. (By reading.)

22. **GDI+ geometry is half a pixel off HTML5.** Its `Graphics` uses
    `PixelOffsetModeNone`, where integer coordinates are pixel centres, so a
    1 px line on an integer coordinate is one crisp row and fills are
    shifted - the opposite of what #92 made Cairo do. Use
    `PixelOffsetModeHalf` (integer coordinates are pixel edges) and run the
    geometry pixel tests on GDI+. Check images and text origins the same
    way. (By reading; confirmed by the tests in step 5.)

Image files:

23. **Cairo and GDI+ prefix `"assets/"` to every image name.** Replace with
    a `FilenameConverter` on every backend's factory (the Quartz shape),
    defaulting to the name unchanged. Synth loads its icons from its
    install directory through this.
24. **Non-ASCII paths fail on Windows** (`stbi_load` uses `fopen`). Define
    `STBI_WINDOWS_UTF8` so paths are UTF-8 on every platform, as synth's
    are.

Build:

25. **`PerlinSurface` doesn't compile**: it calls `Surface`'s constructor
    without the channel count, and doesn't implement `Surface`'s pure
    virtuals (`renderPath`, `renderText`, `measureText`, `drawImage`,
    `createImage`). Pass 4 channels, make the drawing calls no-ops (it is a
    source, not a target), give `createImage()` its pixels, add it to the
    build and test that a drawn noise image reads back non-uniform. Its
    `lockMemory()` also isn't marked `override`.

## Additions

- **Line style:** `lineCap`, `lineJoin`, `miterLimit`, `lineDashOffset`
  (HTML5 drawing state; Cairo, GDI+ and Quartz have all of them).
- **`createRadialGradient()`** and `quadraticCurveTo()`/`bezierCurveTo()`
  (`#if 0` stubs or missing; every backend has the primitives).
- **Palette dithering (step 7):** `FloydSteinberg` gains a palette target:
  the xterm 256-colour palette, or a given palette of up to 256 entries
  (what Sixel can address), producing indices. Add ordered (Bayer)
  dithering beside it: error diffusion changes the whole image when one
  pixel changes, so an animated meter shimmers, while an ordered pattern
  stays put. Built regardless of `CANVAS_TEXTURE_PACKING`, which keeps
  only the 16-bit texture use behind it.
- **Pixel tests per backend:** make `pixel_tests.cpp` take its context from
  a factory so the same tests run on GDI+ in the Windows job (it only
  compiles there now), and add a macOS job that builds the Quartz backend.
  Android stays unbuilt in CI (needs the NDK and a JVM to test).
- **Header names:** the guards are unique now (#95), but canvas's headers
  still include each other as `<Color.h>`, `<Image.h>`, `<Point.h>`, which
  works only with canvas's `include/` on the include path and picks up a
  client's own `Color.h` if that comes first (synth has `model/Color.h`).
  Include each other with quoted relative includes (`"Color.h"`), move the
  headers to `include/canvas/` and install them there, so clients write
  `<canvas/Color.h>`. This breaks every user's includes, so `include/`
  keeps a forwarding header per old name for a release.
- **README:** the fluent example after item 8; drop "used to create OpenGL
  textures" and "Quartz2D on iOS" as the library's description; link the
  geometry and pixel format sections from CLAUDE.md's.

## Steps

1. State: items 1, 2, 7, 8, 9, 12 (small, header-only, no behaviour a
   correct caller relies on).
2. Transforms: items 3, 4, 5, 6, 10, 11 - one PR, since they share the
   decision to transform on entry.
3. Images: items 15, 16, 17, 23, 24, 14. Synth's icons need this one.
4. Colours: item 13.
5. GDI+: the per-backend pixel tests running in the Windows job, then items
   22 and 18-20 against them; item 21 for Cairo.
6. Line style, radial gradients and curves.
7. Palette and ordered dithering.
8. Quoted includes inside canvas and the `include/canvas/` move with
   forwarding headers.
9. `PerlinSurface` (item 25). Independent; can go any time.

Each step also updates canvas's `CLAUDE.md` "Known gaps" and README for
what it closes.

Synth needs 1-4 before its charts move onto canvas (step 2 of
`plans/cell-ui-terminal.md`), 5 before its Windows build draws with GDI+,
and 7 when the terminal backend replaces notcurses (which quantizes Sixel
itself today).
