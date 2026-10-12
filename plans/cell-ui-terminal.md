# Cell UI, phase 1: the terminal

Separate the UI into the Cell UI library (`plans/cell-ui.md`) with only the
terminal backend, replace notcurses with it, and put in the modernizations a
terminal can show (`plans/modern-look.md`). The window backend
(`plans/cell-ui-window.md`) is considered only once this is done. The
Windows port (`plans/windows-port.md`) runs alongside: the two meet when the
terminal backend runs in a Windows console.

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
- Pixel regions go to Kitty graphics or Sixel where available. Otherwise
  the canvas image is quantized to braille or sextant cells. That one
  quantizer replaces the per-widget sub-cell paint code
  (`TerminalHeatmapChart`, the braille path of `SpectrumMeter`) with a
  single path.
- Windows: VT output through ConPTY/Windows Terminal
  (`ENABLE_VIRTUAL_TERMINAL_PROCESSING`), console input records for keys
  (`ReadConsoleInputW`), which report key releases, so held notes work
  there in any console. Built once `plans/windows-port.md` has the platform
  layer (its steps 1 and 3).
- Expect 4-6k lines. It removes notcurses, proves the library API against
  the real app with no new rendering questions, and gives Windows a
  terminal UI.

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

### Synth onto canvas

Move synth's chart and meter paint code onto canvas, drawing each into its
own span's image. The terminal draws that ImageData as pixels or through
the sub-cell quantizer. This deletes custom paint code early and needs no
other part of this plan, so it can go first. Cairo first; GDI+ joins once
the Windows CI job exists (`plans/windows-port.md` step 0). Canvas (MIT)
and its vendored stb headers go into `THIRD_PARTY_LICENSES.md`. (In phase 2
the same images become textures.)

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
2. Synth's charts and meters onto canvas (above). Started: canvas is in the
   build and the pixel spectrum and the pixel heatmap are drawn with it
   (`SpectrumImage`, `HeatmapImage`); the cell paths, the level meters and
   `ChannelMeter` remain.
3. `plans/modern-look.md` steps 1-3: audit, mock, tokens (no visible
   change).
4. Create `lib/cellui` with `UIPlane`/`UIMenu`/the chart and input types
   moved in, the full API defined, and the gallery app, still rendered
   through notcurses behind the new API. Started: the library exists as its
   own project with the dependency-free leaves moved in (`Color`, `utf8`,
   `markdown`, `ChoiceList`, the sub-cell glyphs, then the input data, key chord, keymap
   and command registry); the theme, `UIPlane`, the widget base `Element` and `HeatmapChart` are in too;
   menus, the 1D charts and the gallery app are next.
5. The terminal backend replaces notcurses. Same look, same e2e results.
6. Icons: the app's icon list, Kitty graphics/Sixel/glyph fallback, menu
   icons.
7. `plans/modern-look.md` steps 4-5: the modernized look and the light and
   high-contrast themes.
8. Keymap profiles in the keymap layer, emacs the only one in the
   terminal.
9. Accessibility tree published by the widgets (the terminal uses only
   focus); strings through a catalog.
10. Windows console support in the terminal backend, once
   `plans/windows-port.md` has its platform layer.
