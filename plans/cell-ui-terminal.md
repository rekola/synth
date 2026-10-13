# Cell UI, phase 1: the terminal

Separate the UI into the Cell UI library (`plans/cell-ui.md`) with only the
terminal backend, replace notcurses with it, and put in the modernizations a
terminal can show (`plans/modern-look.md`). The window backend
(`plans/cell-ui-window.md`) is considered only once this is done. The
Windows port (`plans/windows-port.md`) runs alongside; Windows gets only the
graphical UI, never the terminal one.

## Done when

- `lib/cellui` builds on its own, includes nothing from `src/`, and has its
  own tests and a gallery app (every cell style, span, icon, dialog and menu
  as the terminal shows them).
- notcurses is gone from the build; `TerminalUI` talks only to `cellui`.
- The whole API in `plans/cell-ui.md` exists, including what the terminal
  only degrades (style ids, geometry, fractional scroll, plane transforms,
  the accessibility tree), so phase 2 adds a backend without app changes.
- The e2e scripts pass, the look is at least as good as today, and the
  modern-look terminal steps are in.

## Terminal backend (replacing notcurses)

- A cell buffer diffed against the last frame and written as escape
  sequences: the curses model, without curses' screen layer.
- Basic capabilities come from terminfo (libtinfo, i.e. ncurses's terminfo
  database), so old terminals keep working. Newer features are probed at
  startup:
  - device attributes and XTVERSION;
  - the Kitty keyboard protocol;
  - Kitty graphics and Sixel;
  - true colour (COLORTERM, then a query);
  - character width, by printing a probe glyph and reading the cursor
    position (terminals disagree on emoji and the newer symbols).
- Input is our own parser (legacy keys, CSI u, SGR mouse, bracketed paste),
  with `EscapeCoalescer` kept.
- Pixel regions go to Kitty graphics, iTerm2's inline images or Sixel
  where available. Without them a widget draws its cell version by hand
  (see Scopes below); a canvas image is never quantized to braille or
  sextant cells, since a hand-drawn view can put its labels in terminal
  text.
- No Windows console support: Windows uses the graphical UI only.
- Expect 4-6k lines. It removes notcurses and proves the library API
  against the real app with no new rendering questions.

## Icons in the terminal

Drawn as pixels with Kitty graphics or Sixel, whichever the startup probe
found.

- The SVG is rasterized at the terminal's cell size in pixels (from the
  Kitty graphics reply or the window-size report), tinted with the cell's
  foreground and composited onto its background. Sixel's transparency is
  unreliable across terminals, so the background is filled in rather than
  left transparent.
- Kitty graphics: each (icon, colours, size) image is uploaded once under an
  image id and placed by id, which is cheap to repeat. Unicode placeholder
  placements make icons scroll and clear with the text around them.
- Sixel: the encoded image is cached per (icon, colours, size) and re-sent
  whenever its cell is redrawn. The diff renderer treats an icon cell as
  changed when its icon or colours change, and erases a replaced image.
- Fallback: the icon's Unicode glyph (or the generic one) when neither
  protocol is available, when the cell size in pixels is unknown, inside a
  multiplexer that doesn't pass graphics through, or when the user turns
  pixel icons off.
- The SVG is parsed with a small vendored parser (lunasvg or nanosvg) or
  drawn with canvas; the same rasterizer serves the window later.

## 2D drawing: canvas

`rekola/canvas` has the right API: the HTML5 Canvas 2D one, familiar and
fluent, which suits scopes and meters. Its backends are the platforms' own
2D libraries:

- GDI+ on Windows. Its weak spot, unantialiased clipping, doesn't matter
  here: everything is drawn inside a cell rectangle (a span), so no clip
  path is needed.
- Cairo on Linux.
- Quartz, if macOS ever comes.

### Canvas readiness (in the canvas repository)

The core and the Cairo backend compile cleanly as C++17, but the library
isn't ready to be a dependency yet. Before synth uses it:

- **Build.** A `CMakeLists.txt` with one option per backend (Cairo,
  GDI+, Quartz, Android) and an installable, exported target, so synth can
  take it with `find_package` or FetchContent. The Android and Quartz
  backends and the DXT/ETC1 texture compression are off by default, so
  none of them reaches synth's build.
- **Pixel format.** `ImageData`'s layout is defined and documented: byte
  order, and straight or premultiplied alpha. The terminal's Sixel and
  Kitty graphics output and the sub-cell quantizer read it directly.
  Fix the Gaussian blur's handling of premultiplied alpha (its own TODO)
  if shadows are used.
- **Tests and CI.** Pixel tests for what synth draws: a filled rect, an
  antialiased line, a polyline, a linear gradient, an image drawn and read
  back. CI runs them on Linux (Cairo) and on Windows (GDI+, MSYS2 UCRT64).
- **Warnings.** Nothing to change: synth includes canvas's headers as
  SYSTEM includes, as it does for `third_party/`.
- **Text stays out.** Cairo's text path uses its basic font API, with no
  shaping or font fallback. Chart labels and axis numbers therefore stay
  cell text, drawn by Cell UI. Text in canvas waits for phase 2, if it is
  needed at all.
- **Not now:** a GPU backend, removing Cairo, or a DirectWrite path.

### Scopes in Cell UI

The scopes - the spectrum, the DirAC heatmap and the VU meters (the scope
row's channel meter and the level meters in the clip grid and the pattern
editor) - are Cell UI widgets, and each backend provides its own versions of them through
a factory (`cellui::Scopes`), so the app asks for a scope and never knows
how it is drawn:

- The terminal's cell versions are drawn by hand: braille bars, a quadrant
  or sextant heatmap, the axis labels in terminal text. canvas is not used
  for them, not even for sub-cells.
- The terminal's pixel versions, when it shows images, are drawn with
  canvas and sent as images.
- The window backend draws its own (phase 2).

A level meter inside the clip grid or the pattern editor is a small scope
of its own placed over its cells, so it too gets a pixel version. Cairo first; GDI+ joins once the Windows CI
job exists (`plans/windows-port.md` step 0). Canvas (MIT) and its vendored
stb headers go into `THIRD_PARTY_LICENSES.md`. (In phase 2 the same images
become textures.)

Keep canvas a separate library that `cellui` depends on rather than merging
it in. It is useful on its own, and the UI library stays about layout and
input.

## Keymaps

The terminal keeps the emacs keymap only; the standard keymap needs key
combinations a terminal can't send (`plans/cell-ui.md`). What this phase
does is make the keymap layer hold named profiles, with emacs as the one
the terminal uses, so phase 2 adds the standard profile without changing
how commands are bound.

## Modernizations

The terminal steps of `plans/modern-look.md` belong to this phase: the
audit and mock, `StyleProvider` turned into tokens, the new surfaces,
neutrals and track colours, fewer lines, the icon list, fades for state
changes, and the light and high-contrast themes. The tokens step comes
before the terminal backend replaces notcurses, so the new backend is built
against tokens from the start; the visible changes can land on either side
of the switch.

## Steps

1. Canvas readiness, in the canvas repository (above). Independent.
2. Scopes in Cell UI (above). Done: the scopes and both their versions are
   in Cell UI behind `cellui::Scopes` (`cellScopes()`, `pixelScopes()`;
   `cellui::term::scopes()` picks the terminal's), canvas is Cell UI's
   dependency, and the pixel spectrum and heatmap are drawn with it
   (`SpectrumImage`, `HeatmapImage`). Left: the VU meters' pixel versions.
3. `plans/modern-look.md` steps 1-3: audit, mock, tokens (no visible
   change).
4. Create `lib/cellui` with `UIPlane`/`UIMenu`/the chart and input types
   moved in, the full API defined, and the gallery app, still rendered
   through notcurses behind the new API. Done: the library is its own
   project and includes nothing from `src/`. It holds the leaves (`Color`,
   `utf8`, `markdown`, `ChoiceList`, the sub-cell glyphs), the input data,
   key chords, keymap and command registry, the theme, `UIPlane`, the widget
   base `Element`, `UIMenu`, `HeatmapChart`, the generic widgets (choice and
   info dialogs, inline editor, spin box, cover art, the level, channel and
   spectrum meters) and a headless plane (`BufferPlane` over `CellBuffer`)
   with the gallery page that shows them, checked against a golden file.
   What stays in the app is what knows the song or the controller, and the
   notcurses implementations that step 5 replaces.
5. The terminal backend replaces notcurses. Same look, same e2e results.
   Done in the code: notcurses is out of the build and `TerminalUI` runs on
   `lib/cellui`'s terminal backend (`cellui/term/`: `InputParser`,
   `Discovery`/`Capabilities`, `FrameEncoder`, `ImageEncoder`,
   `TerminalSession`), drawing through a root `BufferPlane` (flat children,
   the reader on `LineEditor`, the picker, translucency, images) with the
   menu on `MenuBar`. The display scale is guessed from the cell height
   (`estimateDisplayScale()`, `UIPlane::displayScale()`: text, icons and line
   widths scale by it, hairlines excepted), and the default `--theme`
   follows the terminal's colours (`detectAppearance()`). The non-Launchpad
   e2e scripts give the notcurses build's results or better. iTerm2 gets
   its own inline images (PNG, `OSC 1337`), the spectrum's curve widens
   with the display scale, and key ids have names of our own
   (`cellui::Key`). Confirmed in real terminals: xterm with Sixel, kitty
   (Alt-x, and Escape then x as M-x) and VTE (GNOME Terminal). The
   Launchpad e2e scripts were run on a machine with an ALSA sequencer: four
   fail, to be studied and fixed separately by a local agent that has the
   e2e setup. Still open: the real-terminal
   checks of foot, WezTerm, tmux and iTerm2.
6. Icons: the app's icon list, Kitty graphics/Sixel/glyph fallback, menu
   icons.
7. `plans/modern-look.md` steps 4-5: the modernized look and the light and
   high-contrast themes.
8. Keymap profiles in the keymap layer, emacs the only one in the
   terminal.
9. Accessibility tree published by the widgets (the terminal uses only
   focus); strings through a catalog.
10. A GTK backend for Cell UI on Linux (`plans/cell-ui-window.md`).
11. A test backend: `BufferPlane`'s composition with each frame given out as
   JSON (plane tree, cell text and styles, image placements, focus) and
   input taken from the test, so synth's own tests assert on what the UI
   shows without a pty or a terminal emulator. The terminal layer stays
   covered by Cell UI's backend tests and a few pty smoke tests.
