# cellui

The user interface becomes its own library, **cellui** (`cellui`, working
name - check for a clash before it gets its own repository). The app draws
grids of styled cells, plus pixel regions, and never knows which backend
shows them. There are two backends:

- **Terminal** (`cellui::term`). It replaces notcurses, does the
  terminal-only work itself and keeps compatibility with every terminal.
- **Window** (`cellui::window`). A window on Linux (X11/Wayland), a GTK
  backend there, and on Windows, written directly against Win32 (Direct2D,
  DirectWrite), with real dialogs and menus.

On Linux the app can pick either one when a display server is running;
Windows has only the window.

The work comes in two phases, each with its own plan, while the Windows
port (`plans/windows-port.md`) proceeds alongside:

1. **Terminal cellui** (`plans/cellui-terminal.md`): the library is
   separated out with the terminal backend only, notcurses goes, and the
   modernizations a terminal can show (`plans/modern-look.md`) go in.
2. **Window cellui** (`plans/cellui-window.md`): considered once the UI
   is separated. It chooses a host and adds the graphical backend.

This file is the shared design: why, what the library provides, keymaps
and the long-term risks.

"TUI" and "GUI" stay as the names of the two backends. "cellui" is the
approach: one cell-based UI with two renderings.

## Why this and not Qt, wxWidgets or Dear ImGui

What it costs us: we maintain a small toolkit. What it saves us is
maintaining two UIs. The terminal UI has to stay either way, and with a
toolkit UI every widget (about 6,400 lines today: `PatternEditor`,
`ClipGrid`, `OutlineView`, `ArrangementGrid`) would be written twice, and
every feature after that too. With cellui the widgets stay as they are,
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
app (synth)  ->  cellui  -> terminal backend  (phase 1: VT output, terminfo, probing)
                         -> window backend    (phase 2: host to be chosen)
                         uses canvas          (2D drawing for pixel regions:
                                               GDI+ on Windows, Cairo on Linux)
```

The app talks only to `cellui`. Backend capabilities are queryable, but an
app that ignores them still works: an effect the terminal can't do is
dropped, not an error. Phase 1 defines the whole API below, including the
parts only the window can show, so phase 2 adds a backend without changing
the app.

### What the library provides

Each item says what the terminal does with it; what the window does is in
`plans/cellui-window.md`.

- **Planes (layers).** A grid of cells with z-order, visibility, opacity and
  a transform (a 4x4 matrix, so 3D placement stays possible). The terminal
  honours z-order and visibility. Done: elevation - a plane's height above its
  parent in cell widths (`UIPlane::setElevation()`, the theme's steps panel,
  raised, card, overlay and modal), which orders drawing (lowest first, ties
  in tree order), and a cell's lift above its plane (`UIPlane::setLift()`: the
  clip grid's clips, the pattern editor's track headers), both handed to the backend as the
  areas above the base (`FrameInfo::elevations`), for a display with depth to
  show (shadows, a height field).
- **Cells.** A grapheme cluster (UTF-8; width from `util/Utf8.h`'s rules,
  which move into the library) plus fg, bg, bold, italic and underline, as
  `UIPlane` has now. On top of that comes a **style id** into a style table:
  font role (mono/UI, size, weight), padding, border per edge (spreadsheet
  style), corner radius, background gradient, and shadow. A style degrades
  in the terminal: gradients to their mid colour, borders to box-drawing
  characters only where the style asks for them, everything else dropped.
- **Spans.** A rectangle of cells merged into one, for a label or a pixel
  region. A pixel region is drawn with canvas and re-rendered only when its
  content changes. The terminal shows it with Kitty graphics, iTerm2's
  inline images or Sixel; without them a widget that has a pixel region
  draws its own cell version instead (the scopes, `plans/cellui-terminal.md`).
- **Geometry.** Per-plane column widths and row heights in pixels, so the
  app can say "this row is 1.5 rows high" (a scene header) or "this column
  has 4px of padding". The terminal rounds to whole cells. App code keeps
  addressing cells; events carry both cell and pixel positions.
- **Scrolling.** A plane's scroll offset is fractional. The terminal steps.
- **Windows, dialogs and popups.** A main window, dialogs (modal when asked)
  and popups (menus, tooltips, pickers). In the terminal they are planes
  inside the screen, as now. File dialogs are a library call; the terminal
  answers it with its own picker.
- **Menus.** A menu model (items, submenus, checkable and radio items,
  accelerator text, enabled state, icons), built once by the app; `UIMenu`
  becomes it. The terminal draws it in cells, as now.
- **Text entry.** One single-line text field (the minibuffer, renaming,
  dialog fields): cursor, selection, word movement, clipboard, undo, and
  input-method composition. This is the one real widget in the library, and
  the one that takes the most care.
- **Input.** Key down and key up with physical key and modifiers, kept
  separate from text input (keyboard layouts, input methods); mouse with
  pixel and cell coordinates and precise wheel deltas; file drops.
- **Icons.** The app registers its own icons by id: a vector source (SVG),
  drawn as pixels in both backends, a name, and optionally a Unicode
  fallback glyph for terminals without pixel graphics. An icon with no glyph
  of its own falls back to one shared generic icon glyph, so any icon can be
  added without first finding a Unicode character for it; the generic glyph
  must not be one that already carries a meaning (not ◆, ●, ▸ and so on).
  An icon takes one or more cells, like any other cell content.
  - Accessibility: the glyph is also what a screen reader or braille
    display gets in the terminal, and a Unicode character is understood
    everywhere, so an icon that matters for using the program should have
    its own glyph, not the generic one. The accessibility tree reports the
    icon's name, since screen readers name symbols inconsistently.
  - The same ids name menu item icons and, where the hardware allows, the
    Launchpad's own meanings, so one icon list covers every surface.
- **Clipboard:** the system clipboard. The terminal uses OSC 52 where the
  terminal allows it.
- **Accessibility tree:** each widget publishes roles (grid, row, cell,
  button, slider, text field), names, values and focus. The terminal keeps
  the real terminal cursor on the focused cell, which is what terminal
  screen readers follow; the window hands the tree to the platform.
- **Themes:** the style table and the colour tokens (`StyleProvider`) are
  data.

## Where it lives

1. A library target inside this repository (`lib/cellui/`, its own
   `CMakeLists.txt`, no `#include` of anything in `src/`), with its own tests
   and a small gallery app that shows every cell style, span, dialog and menu
   in each backend.
2. Move it to its own repository once the API has stopped moving, the way
   synth itself was extracted from `personal`. Not before: while the app is
   the only user, changing both in one commit is the fast path.

`UIPlane`, `UIMenu`, `Chart`, `HeatmapChart` and the input event become
library types or thin aliases. `UI` and `TerminalUI` stay in the app,
`TerminalUI` turning into the app's layout, now backend-neutral.

## Keymaps

- Two keymap profiles:
  - **emacs:** today's bindings. The terminal's only keymap, and selectable
    in the window too. It exists because a terminal can't send the
    combinations the standard keymap needs: without the Kitty protocol
    Ctrl+Shift chords don't arrive at all, several Ctrl keys collapse into
    one byte, and Ctrl-C, Ctrl-Z and Ctrl-S belong to the terminal itself
    (interrupt, suspend, flow control). Emacs's prefix keys work within
    what every terminal sends.
  - **standard:** the window's default, window only. It is built from
    three layers:
    - desktop conventions: Ctrl-C/X/V/Z, Ctrl-Shift-Z and Ctrl-Y for
      redo, Ctrl-S/O/N, Delete, shift-selection, F1;
    - live-sequencer conventions: Tab for the view, Space to play,
      duplicate and the clip commands;
    - tracker conventions in the pattern editor.
- Commands, M-x and the menus are the same in both keymaps and both
  backends. The standard keymap
  also gets a command palette key bound to the same minibuffer (key to be
  decided).
- The standard bindings are the ones those conventions share and their
  users already know, chosen once from that common knowledge and documented
  as this program's own bindings in `docs/user-interface.md`. No binding is
  copied from another program's manual, and no binding is credited to one.
- In the standard keymap a selection uses the system clipboard. The region
  model (there's always a region) stays.
- The keymap layer holds both profiles from phase 1 on, so the standard one
  is added in phase 2 without changing how commands are bound.

## What custom toolkits usually lack (and our answer)

These are the problems that only show after years. Each one gets a decision
now, even if the work comes later.

| Area | What goes wrong | Our answer |
|---|---|---|
| Accessibility | Screen readers see an opaque canvas; the app is unusable for blind users, and the gap grows with every widget | Accessibility tree from day one (roles from the cell model), AccessKit adapters; the terminal backend keeps the real cursor on focus |
| Text input / IME | CJK input, dead keys, compose and emoji pickers break; text fields lack word movement, undo and selection | One text field done properly, input methods via the window host; no other text widgets |
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
- **Lean on mature libraries** for the hard platform parts: the window host
  (`plans/cellui-window.md`), canvas (GDI+/Cairo).
- **Order:** the terminal backend first (phase 1). It replaces a dependency
  that's already broken, so that cost has to be paid anyway. The window
  backend comes second (phase 2), behind a flag, while the terminal stays
  the default until the window one is better.
- **Rough size:**
  - terminal backend: 4-6k lines;
  - window backend core (windows, cells, text, atlas, input, dialogs,
    popups, menus): 8-12k lines;
  - accessibility, IME and i18n: ongoing.
- **Comparison:** a Qt UI is a full second front end (widgets ~6k lines
  now and growing with every feature), plus Qt's build and licence
  obligations. That is the cost being traded against.
