# Cell UI effects: what the grid is for

The window backend is Cell UI too (`plans/cell-ui.md`, `plans/cell-ui-window.md`).
This plan is about what that buys beyond "one UI for two backends": things a
widget toolkit makes hard, and a look that is modern rather than dated
(`plans/modern-look.md` covers the tokens; this covers the effects).

Status: under discussion. Steps 1 and 2 (backdrop blur under modal dialogs,
in cells and in pixels) are built on `claude/cell-ui-backdrop-blur`.

## The selling point

**The screen is a grid of meanings, not of pixels.** Every cell knows what it
is (a note, a row, a track's header, a playing clip) and where it was last
frame. A widget toolkit has neither: effects there are either per-widget code
(a delegate, an animation object per item) or a post-process over pixels that
knows nothing about what it is blurring. From that follow four things that
are hard elsewhere and nearly free here:

1. **Effects address meaning.** "Ghost the notes that just played", "shake the
   cell that refused the input", "assemble this text out of particles that land
   in its real cells" are a filter over cells with a role, written once for
   every widget.
2. **Time is free.** The compositor already has this frame and the last one
   (the terminal's frame diff). Phosphor ghosting, change highlights and
   inertia are generic: no widget does anything for them.
3. **One effect, two renderings.** Each effect has a cell version and a pixel
   version. The cell version runs over SSH; nobody else blurs a terminal. The
   cell version is also a text golden test of the pixel one's intent.
4. **Lo-fi is native, not faked.** Ordered dither, braille or sextant particles
   and one-cell offset shadows are what a cell can do exactly. A pixel UI
   imitating them looks like a filter; here they are the material.

What is harder with cells, and the answer: proportional text (input fields
and tooltips leave the grid in the window, below), smooth motion (the window
owns fractional offsets; the terminal steps), and an honest blur in the
terminal is coarse (a radius of one row is a 5x3-cell average).

## The model: the grid is a cube

Planes are already layers; the third axis is the stack of layers over each
cell. An effect is an attribute of a cell in some layer, resolved per cell
through the stack by the compositor (`BufferPlane::compose()`), so the app
keeps addressing cells and never draws an effect itself:

| Attribute | Meaning | Status |
|---|---|---|
| backdrop | blur and dim what is below (`Backdrop`) | built |
| opacity | `bg_blend` today, generalised to an amount | partly (`bg_blend` = 50%) |
| border per edge | style: line, dashed, marching ants (with phase), drag handle | planned |
| span | row/colspan into a supercell: text, pixels, a 3D view | planned (pixel regions exist) |
| gradient | colours at the span's corners, interpolated per cell (per pixel in the window) | planned |
| shader | an id plus parameters; a CPU function of (cell, time) in the terminal, a fragment shader on the cell quad in the window | planned |
| decay | how long the cell's previous content glows after it changes (phosphor) | planned |

## Catalogue

Each idea with its cell (terminal) and pixel (window) rendering:

| Effect | Cell rendering | Pixel rendering | Notes |
|---|---|---|---|
| Backdrop blur under modals | with an image protocol: the cells below drawn as pixels, blurred, in tiles; otherwise the average of each cell's background and ink over a box twice as wide as tall, glyphs fading into it | gaussian of the layer below | steps 1-2 |
| Borders, drag handles | box drawing; dashed `┄┆`; handles as `⠿` | SDF strokes | |
| Marching ants | dashed border whose phase advances on the frame clock | the same, dash pattern in pixels | selection region |
| Supercells (spans) | merged text cell; a pixel region where the terminal has images | texture on a quad, a 3D view | |
| Background gradients | per-cell interpolated background in truecolour; half blocks double the vertical steps | interpolated per pixel | |
| Per-cell shaders | CPU function per cell per frame, only for animated cells | fragment shader | the general hook for the rest |
| Tooltips free of the grid | cells, placed by the pointer's cell | pixel-positioned popup windows | |
| Contextual shakes | the plane moved a column for a few frames | spring with sub-pixel offsets | refused input, a clip that can't launch |
| Input fields with a UI font | monospace field | a span drawn with the UI font, off the grid | the one text widget |
| Pattern editor inertia | rows step on an eased timeline | fractional scroll offset with a spring | not for the playhead, which is time itself |
| Phosphor ghosting | a changed cell's old colour decays over frames | the same, plus bloom | the playhead's trail, notes just played |
| Particles forming text or layers | braille (2x4) or sextant (2x3) dots converging on the glyph cells | sprites | panel open, song loaded |
| Particle shadows | a dithered cloud of dots | the same, soft | |
| Bayer dither shadows | `░▒▓` or braille density by an ordered (Bayer) matrix | Bayer matrix shader | identical look in both |

## Aesthetic

Of the three directions, **Hi-Fi Retro** fits best: an honest grid, phosphor,
dither, and measured (the modern-look tokens stay; this is
motion and material, not new colours). From **neo-brutalism** take the hard,
one-cell offset shadow: it is a cell operation, exactly. **Cyberpunk**'s
saturated neon fights the measured palette and the shared Launchpad colours,
so at most a theme of its own later.

## Rules

- Effects never move or delay the playhead or a meter, and never touch the
  audio thread.
- Reduced motion turns off everything that moves (shakes, inertia, particles,
  ants' march, decay); the static form stays (a dashed border, no trail).
- An idle UI stays idle: every animation ends, and nothing is drawn after.
- Effects are cell attributes resolved by the compositor, never drawn by a
  widget, so a widget gets them without code and the terminal and window
  agree.

## Steps

1. Backdrop blur and dim under modal dialogs: `Backdrop` cell attribute,
   `Scrim`, `Theme::modal_backdrop`, images filtered once while covered. Done
   on the branch.
2. The backdrop in pixels, in the terminal. Done on the branch:
   - `CellRasterizer` draws composed cells as pixels (`canvasRasterizer()`):
     backgrounds, block elements, braille and images drawn directly with
     exact pixel coverage, text with canvas in runs, in the system's
     monospace font (fontconfig's `monospace`), sized so its advance is a
     cell's width. The window backend's cell renderer starts here.
   - A blurring backdrop is tiles of 4 x 16 cells, each drawn with its blur
     margin at a fraction of the display's resolution, blurred, dimmed and
     stretched by the terminal over its cells (`Image::cols`/`rows`; kitty's
     `c=`/`r=`, iTerm2's cell size). A tile is drawn again only when a
     fingerprint of the cells and pictures it reads changes, so the
     background keeps running under the dialog and static areas are not sent
     again.
   - kitty and iTerm2's protocol only; Sixel keeps the cell blur (it can't
     stretch a picture, and its palette would band the blur).
   - Measured on a 200 x 50 screen of 10 x 20 pixel cells: opening 24 ms and
     328 KB (169 tiles), a playhead row moving 4.8 ms and 56 KB a frame (28
     tiles). If that is too much: draw a dirty band once and cut its tiles
     (the margins are now drawn three times over), lower the rate, or blur
     more (smaller pictures).
   - Next: the terminal's own font. kitty answers it through XTGETTCAP
     (`kitty-query-font_family`, `kitty-query-font_size`); elsewhere a user
     setting, falling back to `monospace`.
3. Dialog elevation over the scrim: border contrast and a one-cell offset
   (brutalist) or dithered shadow.
4. Border styles per edge, marching ants for the region.
5. Phosphor decay (the frame diff) on the pattern editor.
6. Gradients and spans formalised; shader hook.
7. Particles, shakes, inertia: with the window backend's frame clock.
