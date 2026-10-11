# Cell UI

The user interface becomes its own library, **Cell UI** (`cellui`, working
name - check for a clash before it gets its own repository). The app draws
grids of styled cells, plus pixel regions, and never knows which backend
shows them. There are two backends:

- **Terminal** (`cellui::term`). It replaces notcurses, does the
  terminal-only work itself and keeps compatibility with every terminal.
- **Window** (`cellui::gpu`). A GPU-rendered window on Linux (X11/Wayland)
  and Windows, with real dialogs and menus.

The app can pick either one when a display server is running.

"TUI" and "GUI" stay as the names of the two backends. "Cell UI" is the
approach: one cell-based UI with two renderings.

## Why this and not Qt, wxWidgets or Dear ImGui

What it costs us: we maintain a small toolkit. What it saves us is
maintaining two UIs. The terminal UI has to stay either way, and with a
toolkit UI every widget (about 6,400 lines today: `PatternEditor`,
`ClipGrid`, `OutlineView`, `ArrangementGrid`) would be written twice, and
every feature after that too. With Cell UI the widgets stay as they are,
already drawing only through `UIPlane` (notcurses is confined to
`TerminalUI.cpp`).

What it gives the customer (the selling points to build the look and the
docs around):

1. **Keyboard-first, the same everywhere.** A tracker's density and speed,
   with one UI from an SSH session on a headless box or a Raspberry Pi in an
   installation to a desktop window. Learn it once. Every action is a named
   command, so M-x, the menus, keys, the Launchpad and (later) voice all reach
   the same thing.
2. **Spatial.** The engine is ambisonic. The window backend's layers have
   depth, and later can be placed in 3D (below). That makes "see where you
   hear it" possible: a track's panel sits where its sound is. No DAW does
   this, and it is a reason for depth and floating layers beyond decoration.
3. **Accessible.** A tracker is a table, and the cell model knows which cell
   is a note, a row or a track. That maps straight onto a screen reader's
   grid and table roles. DAWs and trackers are mostly poor here, and the
   terminal backend already works with a terminal's screen reader. It is a
   genuine differentiator if we keep it in from the start (below).

Be honest about what is *not* toolkit-specific: voice commands, the Launchpad
and installation controls come from the command architecture and would work
under Qt too. The toolkit's own selling point is (1). It is also what makes
(2) cheap: a cell grid is a texture that can be put on any quad. (3) is
cheap because the semantics are already there.

## Ideas, ranked

- **Now (design for it):** layers with z-order, opacity and a transform;
  drop shadows and blur behind floating layers. Smooth scrolling and
  playheads. Gradients, borders and rounded corners as cell styles.
- **Soon after the window backend:** voice commands. whisper.cpp (MIT,
  offline, CPU) turns speech into a command name or a command with an
  argument ("launch scene three", "transpose up"), shown in the minibuffer
  before it runs. It is cheap because the command registry already names
  everything.
- **Later, once layers have 3D transforms:** a headset mode. Panels float
  around the listener at their tracks' ambisonic positions, and the binaural
  decoder follows head tracking. Out of scope until there's a reason to buy
  the hardware; the design rule now is that a plane's transform is a 4x4
  matrix, not just an x/y offset.
- **Installations and escape rooms:** generalize the Launchpad into "a
  control bound to a command". It could be a MIDI note, an OSC address, a
  GPIO pin or a projected region, with a fullscreen kiosk mode and
  `--headless` on a box with no screen at all. Mostly outside the UI
  library; the library only needs kiosk mode (no chrome, no menus, no quit
  key).

## Architecture

```
app (synth)  ->  cellui  -> term backend   (VT output, terminfo, probing)
                         -> gpu backend    (SDL3, SDL_GPU, HarfBuzz/FreeType)
                         uses canvas       (2D drawing for pixel regions:
                                            GDI+ on Windows, Cairo on Linux)
```

The app talks only to `cellui`. Backend capabilities are queryable, but an
app that ignores them still works: an effect the terminal can't do is
dropped, not an error.

### What the library provides

- **Planes (layers).** A grid of cells with z-order, visibility, opacity and
  a transform. The terminal backend only honours z-order and visibility.
- **Cells.** A grapheme cluster (UTF-8; width from `util/Utf8.h`'s rules,
  which move into the library) plus fg, bg, bold, italic and underline, as
  `UIPlane` has now. On top of that comes a **style id** into a style table:
  font role (mono/UI, size, weight), padding, border per edge (spreadsheet
  style), corner radius, background gradient, and shadow. A style degrades
  in the terminal: gradients to their mid colour, borders to box-drawing
  characters only where the style asks for them, everything else dropped.
- **Spans.** A rectangle of cells merged into one, for a label or a pixel
  region. A pixel region is drawn with the canvas API (below) and is
  re-rendered only when its content changes.
- **Geometry.** Per-plane column widths and row heights in GPU pixels, so
  the app can say "this row is 1.5 rows high" (a scene header) or "this
  column has 4px of padding". The terminal backend rounds to whole cells.
  App code keeps addressing cells. Pixel positions are for the backend, and
  for the mouse (events carry both).
- **Scrolling.** A plane's scroll offset is fractional. The GPU backend
  glides with vsync; the terminal steps.
- **Windows.**
  - The main window.
  - **Dialogs:** separate OS windows with a real title bar, draggable,
    modal when asked (`SDL_SetWindowParent` + `SDL_SetWindowModal`). Their
    content is cells and canvas like everything else.
  - **Popups:** menus, tooltips and pickers, as `SDL_CreatePopupWindow`, so
    they can leave the main window's area (Wayland `xdg_popup` included).
  - In the terminal, dialogs and popups are planes drawn inside the screen,
    as now.
- **Native dialogs** for files (open/save/folder: `SDL_ShowOpenFileDialog`
  and friends, portal-backed on Linux) and message boxes.
- **Menus.** A menu model (items, submenus, checkable and radio items,
  accelerator text, enabled state), built once by the app; `UIMenu`
  becomes it.
  - Windows: a real `HMENU` on the main window (`SetMenu` on the HWND SDL
    exposes).
  - Linux: no native menu bar exists outside a toolkit, so the library
    draws one that looks native, as popup windows with the system font,
    not as cells. It takes the desktop's colour scheme, accent and font
    from the desktop portal's settings and fontconfig. Not GTK: a GTK menu
    needs a GTK parent widget and GTK's main loop, on Wayland it cannot
    attach to another toolkit's surface, and it would look like GNOME on
    KDE anyway. Exporting the menu over D-Bus for desktops with a global
    menu bar (KDE Plasma's) is a cheap addition later.
  - macOS, if ever: `NSMenu`.
  - Terminal: drawn in cells, as now.
- **Text entry.** One single-line text field (the minibuffer, renaming,
  dialog fields): cursor, selection, word movement, clipboard, undo, and
  IME composition (`SDL_EVENT_TEXT_EDITING`). This is the one real widget
  in the library, and the one that takes the most care.
- **Input.**
  - Key down and key up with physical key and modifiers, kept separate from
    text input (keyboard layouts and IME).
  - Mouse with pixel and cell coordinates, precise wheel deltas, and file
    drops (samples onto a slot).
  - Windows console input (`ReadConsoleInputW`) also reports key releases,
    so the terminal backend gets held notes there for free.
- **Icons.** The app registers its own icons by id: a vector source
  (SVG), drawn as pixels in both backends, a name, and optionally a Unicode
  fallback glyph for terminals without pixel graphics. An icon with no glyph
  of its own falls back to one shared generic icon glyph, so any icon can be
  added without first finding a Unicode character for it; the generic glyph
  must not be one that already carries a meaning (not ◆, ●, ▸ and so on).
  An icon takes one or more cells, like any other cell content.
  - Window: rasterized at the current scale into the glyph atlas and tinted
    by the cell's foreground colour, so icons follow the theme and state
    colours. The SVG is parsed with a small vendored parser (lunasvg or
    nanosvg) or drawn with canvas.
  - Terminal: drawn as pixels with Kitty graphics or Sixel, whichever the
    startup probe found.
    - The SVG is rasterized at the terminal's cell size in pixels (from the
      Kitty graphics reply or the window-size report), tinted with the
      cell's foreground and composited onto its background. Sixel's
      transparency is unreliable across terminals, so the background is
      filled in rather than left transparent.
    - Kitty graphics: each (icon, colours, size) image is uploaded once
      under an image id and placed by id, which is cheap to repeat.
      Unicode placeholder placements make icons scroll and clear with the
      text around them.
    - Sixel: the encoded image is cached per (icon, colours, size) and
      re-sent whenever its cell is redrawn. The diff renderer treats an
      icon cell as changed when its icon or colours change, and erases a
      replaced image.
    - Fallback: the icon's Unicode glyph (or the generic one) when
      neither protocol is available, when the cell size in pixels is
      unknown, inside a multiplexer that doesn't pass graphics through, or
      when the user turns pixel icons off. An icon with its own glyph
      keeps its meaning in a plain terminal.
  - Accessibility: the glyph is also what a screen reader or braille
    display gets in the terminal, and a Unicode character is understood
    everywhere, so an icon that matters for using the program should have
    its own glyph, not the generic one. The accessibility tree reports the
    icon's name, in both backends, since screen readers name symbols
    inconsistently.
  - The same ids name menu item icons and, where the hardware allows, the
    Launchpad's own meanings, so one icon list covers every surface.
- **Clipboard:** the system clipboard in both backends. The terminal uses
  OSC 52 where the terminal allows it.
- **Accessibility tree:** each widget publishes roles (grid, row, cell,
  button, slider, text field), names, values and focus. The GPU backend
  exposes it through AccessKit (C bindings over UI Automation on Windows
  and AT-SPI on Linux). The terminal backend keeps the real terminal cursor
  on the focused cell, which is what terminal screen readers follow.
- **Themes:** the style table and the colour tokens (`StyleProvider`) are
  data. The OS dark/light preference, high contrast and reduced motion are
  read in the GPU backend.

### Rendering (GPU backend)

- **SDL3** handles windows, input, IME, dialogs, popups, clipboard and file
  drops on Linux (X11/Wayland) and Windows. It is in MSYS2, and is vendored
  via FetchContent where the distro has none (Ubuntu 24.04).
- **Use SDL_GPU, not raw OpenGL.** Yes, render on the GPU, but through
  SDL_GPU (Vulkan on Linux, D3D12 on Windows, Metal if macOS ever comes):
  - OpenGL is deprecated on macOS, and Windows OpenGL drivers are the least
    reliable path there.
  - Software rasterizers cover machines without a GPU (WARP on Windows,
    lavapipe on Linux), so there is no separate fallback renderer to keep.
- **Speed isn't the reason.** A few thousand cells is nothing for any GPU
  path. The point is smooth frame pacing, effects, and an idle UI that
  draws nothing (the engine side already sends nothing when idle; keep it
  that way).
- **One instanced draw for cells:** a quad per cell, glyphs from an atlas.
- **SDF shaders** for rounded rects, borders, gradients and shadows. Blur
  for floating layers is a two-pass shader on that layer's texture.
- **Pixel regions are textures,** uploaded only when changed.
- **Text:** FreeType + HarfBuzz everywhere (one stack, real shaping),
  fontconfig fallback on Linux, DirectWrite font fallback lookup on
  Windows. Bundle a UI font and a mono font (both OFL) so the look is the
  same everywhere; system fonts cover scripts they lack.
- **HiDPI:** per-monitor scale from SDL; the atlas is rebuilt per scale.

### Terminal backend (replacing notcurses)

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
  (`ENABLE_VIRTUAL_TERMINAL_PROCESSING`), console input records for keys.
- Expect 4-6k lines. It's the first thing to build: it removes notcurses,
  proves the library API against the real app with no new rendering
  questions, and gives Windows a terminal UI.

### 2D drawing: canvas

`rekola/canvas` has the right API: the HTML5 Canvas 2D one, familiar and
fluent, which suits scopes and meters. Its backends are the platforms' own
2D libraries:

- GDI+ on Windows. Its weak spot, unantialiased clipping, doesn't matter
  here: everything is drawn inside a cell rectangle (a span), so no clip
  path is needed.
- Cairo on Linux.
- Quartz, if macOS ever comes.

Plan:

1. **Now (no UI change needed):** move synth's chart and meter paint code
   onto canvas, drawing each into its own span's image. The terminal draws
   that ImageData as pixels or through the sub-cell quantizer. This deletes
   custom paint code early, independent of everything else here.
2. **With the GPU backend:** the images become textures, uploaded only when
   they change. A canvas GPU backend for the subset synth draws (rects,
   rounded rects, antialiased lines and polylines, gradients, images, text
   through the same atlas, shadows) can come later, if CPU drawing plus
   upload ever shows up in profiles.

Keep canvas a separate library that `cellui` depends on rather than merging
it in. It is useful on its own, and the UI library stays about layout and
input.

## Where it lives

1. A library target inside this repository (`lib/cellui/`, its own
   `CMakeLists.txt`, no `#include` of anything in `src/`), with its own tests
   and a small gallery app that shows every cell style, span, dialog and menu
   in both backends.
2. Move it to its own repository once the API has stopped moving, the way
   synth itself was extracted from `personal`. Not before: while the app is
   the only user, changing both in one commit is the fast path.

`UIPlane`, `UIMenu`, `Chart`, `HeatmapChart` and the input event become
library types or thin aliases. `UI` and `TerminalUI` stay in the app,
`TerminalUI` turning into the app's layout, now backend-neutral.

## Keymaps

- Two keymap profiles, both available in both backends:
  - **emacs:** today's bindings, the terminal's default.
  - **standard:** the window's default. It is built from three layers:
    - desktop conventions: Ctrl-C/X/V/Z, Ctrl-Shift-Z and Ctrl-Y for
      redo, Ctrl-S/O/N, Delete, shift-selection, F1;
    - live-sequencer conventions: Tab for the view, Space to play,
      duplicate and the clip commands;
    - tracker conventions in the pattern editor.
- Commands, M-x and the menus are the same in both. The standard keymap
  also gets a command palette key bound to the same minibuffer (key to be
  decided).
- The standard bindings are the ones those conventions share and their
  users already know, chosen once from that common knowledge and documented
  as this program's own bindings in `docs/user-interface.md`. No binding is
  copied from another program's manual, and no binding is credited to one.
- In the standard keymap a selection uses the system clipboard. The region
  model (there's always a region) stays.
- In the terminal, the standard keymap loses what the terminal can't send
  (Ctrl+Shift chords without the Kitty protocol, Ctrl-C/Ctrl-Z unless raw
  mode takes them). That goes in the Terminal limitations section.

## What custom toolkits usually lack (and our answer)

These are the problems that only show after years. Each one gets a decision
now, even if the work comes later.

| Area | What goes wrong | Our answer |
|---|---|---|
| Accessibility | Screen readers see an opaque canvas; the app is unusable for blind users, and the gap grows with every widget | Accessibility tree from day one (roles from the cell model), AccessKit adapters; the terminal backend keeps the real cursor on focus |
| Text input / IME | CJK input, dead keys, compose and emoji pickers break; text fields lack word movement, undo and selection | One text field done properly, IME via SDL3; no other text widgets |
| Internationalization | Strings hard-coded in code; no plural forms; layouts assume English widths; no right-to-left | Strings through a catalog (gettext-style, ids in code); cell widths from text measurement, never assumed; right-to-left: labels only (note data stays left-to-right), decided when a language needs it |
| Fonts | Missing glyphs (tofu), no shaping, no fallback | HarfBuzz + FreeType + system fallback; bundled UI/mono fonts |
| HiDPI / multi-monitor | Blurry text, wrong size when moving between monitors | Per-monitor scale, atlas per scale |
| Platform integration | No native menus or dialogs, wrong shortcuts (Cmd vs Ctrl), no dark-mode follow, no recent files | Native menus on Windows, native-looking ones on Linux, native file dialogs, OS theme and reduced-motion follow, standard keymap |
| Power | Immediate-mode UIs redraw at 60 fps forever | Retained cells with damage tracking; draw only on change or animation |
| Remote desktop / VMs | GPU path missing or slow | WARP/lavapipe fallback; the terminal backend for SSH |
| Testing | UI bugs found by hand | Offscreen rendering to images with tolerant golden tests on CI (lavapipe/WARP); the existing pty-based e2e scripts keep covering the terminal |
| Docs and community | Nobody else knows the toolkit | Small API, gallery app, docs in the library |

Dear ImGui specifically gives up most of the left column by design. It is
excellent for tools and debug panels, and weak on IME, accessibility,
native menus and dialogs, and idle power. Qt and wxWidgets have most of it,
at the price of a second UI.

## Will it slow development down?

Yes, for a while, and bugs in the library are ours to fix. The plan keeps
that bounded:

- **Scope:** a cell grid, spans, styles, planes, one text field, menus,
  dialogs, popups. No general widget set, no layout engine beyond the
  grid. Anything else stays app code.
- **Lean on mature libraries** for the hard platform parts: SDL3, HarfBuzz,
  FreeType, AccessKit, canvas (GDI+/Cairo).
- **Order:** the terminal backend first. It replaces a dependency that's
  already broken, so that cost has to be paid anyway. The window backend
  comes second, behind a flag, while the terminal stays the default until
  the window one is better.
- **Rough size:**
  - terminal backend: 4-6k lines;
  - window backend core (windows, cells, text, atlas, input, dialogs,
    popups, menus): 8-12k lines;
  - accessibility, IME and i18n: ongoing.
- **Comparison:** a Qt UI is a full second front end (widgets ~6k lines
  now and growing with every feature), plus Qt's build and licence
  obligations. That is the cost being traded against.

## CI and installer

- CI builds both backends on Linux and Windows (`plans/windows-port.md` step
  0 sets up the Windows job). Golden-image tests of the gallery app run
  through software Vulkan (lavapipe) on Linux and WARP on Windows.
- **Windows installer:** Inno Setup, built on CI from tags.
  - It bundles the MSYS2 runtime DLLs the binary needs (collected with
    `ntldd`), the default song, a General MIDI SoundFont (FluidR3_GM, MIT),
    a SOFA file if its licence allows, and the licence texts.
  - Start menu entries for the window and the terminal.
  - Later: an MSIX or winget manifest.
- **Code signing:** an unsigned installer triggers SmartScreen warnings. A
  signing service (Azure Trusted Signing or similar) runs in the release
  job once there are users to protect.
- **Linux:** AppImage or Flatpak later; distro packages need no installer.

## Steps

1. Canvas for synth's charts and meters (GDI+ on Windows, Cairo on Linux),
   terminal draws ImageData via pixels or the sub-cell quantizer. Independent of the rest.
2. Create `lib/cellui` with `UIPlane`/`UIMenu`/input types moved in and the
   gallery app; the terminal backend replaces notcurses
   (`TerminalUI.cpp`'s planes, input, menus, charts). Same look, same
   e2e results.
3. Keymap profiles (emacs/standard) in the app's keymap layer, the standard
   one built from verified bindings, usable in the terminal already.
4. GPU backend: main window, cells, text, input, popups, dialogs, file
   dialogs, menus (native on Windows), `--ui window|terminal`, default
   chosen by whether a display is available.
5. Styles beyond the terminal: geometry, spans, borders, gradients, radius,
   shadows, smooth scrolling. The look comes from
   `plans/modern-look.md`.
6. Accessibility tree and AccessKit; string catalog.
7. Icons: the app's icon list (SVG plus terminal fallback) in both
   backends and the menus.
8. Windows installer and signing.
9. Voice commands (whisper.cpp), then layers in 3D and the rest of the
   ideas above as wanted.
