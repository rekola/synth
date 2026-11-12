# cellui — the UI toolkit

`lib/cellui/` is its own CMake project (`cmake -S lib/cellui -B build-cellui`
builds and tests it alone, which CI does). It includes nothing from `src/`: a
ctest (`cellui_includes_nothing_from_src`) fails on any include that leaves
it. Headers are `cellui/X.h`, in `namespace cellui`. Tests in
`lib/cellui/tests/` use a copy of the tiny test framework (no dependency on
`tests/`). cellui owns the libunistring and canvas dependencies.

What the app adds on top is in `src/ui/CLAUDE.md`; the app's `UIElement`
extends `Widget` with the event handler and the controller, and `Plane`
carries the app's object as an opaque `getAppContext()`.

## Leaves

- `Color`: linear Rec.2020 light as floats, 1.0 = SDR white, never negative,
  above 1 is HDR. Built from `#rgb`/`#rrggbb`, `oklch()`,
  `color(srgb|srgb-linear|display-p3|rec2020 ...)`, HSL or linear values; an
  sRGB-only backend takes `toSrgb8()` (gamut-mapped; light past white is scaled to
  fit and washes toward white the more there is, `ToneMap.h`'s `neutralToneMap()`:
  Khronos PBR Neutral's compression and desaturation, started at 1 so display
  white stays white). `mix()`
  is in linear light (the compositor, `bg_blend`, backdrops); `blend()` is a
  widget's tween in encoded sRGB, where the theme was tuned.
- `NumberParse.h`: `parseFloat()` is `std::from_chars` on every platform,
  never the locale (a window toolkit sets it, and a decimal comma reads
  "0.7" as 0); vendored fast_float where the standard library lacks it
  (libc++ below macOS 26.0). Use it, never `strtof`/`atof`/`istringstream`.
- `utf8` (grapheme-aware width, truncation, padding), `markdown` (the info
  dialogs' tiny subset), `Digit.h`, `Signal.h` (`Signal<Args...>` + RAII
  `Connection`: point-to-point, synchronous on the UI thread, tolerant of
  handlers that disconnect, connect, re-emit or destroy the owner
  mid-emission).
- Input: `Input.h` (key id, modifiers, position, press/release kind;
  `Key::Up` etc.), key stroke packing, `Keymap`, `CommandRegistry`,
  `KeymapProfile`. `EscapeCoalescer.h`/`KeyReader` fold a bare ESC followed
  arbitrarily later by a key into one Meta event (Emacs's indefinite
  ESC-as-Meta; no timing window) and know when the "ESC-" indicator is due.
- Theme (`Theme`, `StyleProvider`), `Plane`, `Widget` (`Widget.h`: the plane,
  drawing, keymap and prefix-key dispatch),
  `UIMenu`, `Viewport.h`, the generic widgets (`ChoiceList`/`ChoiceDialog`,
  `InlineEditor`, `SpinBox`, `Dial`, `LineChart`, `HeatmapChart`, `InfoDialog`,
  `CommandPalette` with `FuzzyMatch.h`, `ModalDialog`), `Gallery.h`.

## Events and input dispatch

- `EventTypes.h`: an event is any struct; widgets form a tree via
  `attach()`/`detach()`; `Widget::on<E>()`, `post()` (targeted capture then
  bubble) and `postToRoot()` (tree-order dispatch through a per-root
  registry). `InputEvents.h` has `KeyDown`, `KeyUp`, `MouseDown`, `MouseUp`,
  `Wheel`, `Enter`, `Leave`, `FocusIn`, `Resize`, ... as plain structs; a
  widget takes input as these (`on<KeyDown>()` etc.) or as plain values from
  its owner (`LineEditor::key()`, `ChoiceDialog::click()`, `Dial::press()`),
  never an `Input`. `Input` is what a display reports; only the root
  (`InputDispatcher::offer()`) and the key reader see it.
- `InputDispatcher.h`: keys go to the focused widget; pointer events to the
  topmost widget in `BufferPlane::takeHitAreas()`, with a grab from press to
  release, `Enter`/`Leave` from successive hit tests, and focus traversal.
  The root (the app's `InteractiveUI`) feeds it each frame's hit areas, sees
  every pointer event first. A drag is a `MouseMove` with the held buttons
  in `buttons`, to the widget that got the press (the grab), on every
  display: the terminal's "press again" is turned into motion in `toInput()`. Modal layers, the menu and the status line are capture handlers on
  the root ahead of the focused widget, so a key is offered to the dispatcher
  once. A fresh left press activates the widget under it in a root capture
  handler unless a modal or the menu swallows it. `KeyDown::release_reported`
  says the display reports this key's release (a preview note held by a key
  needs it). `Input::getFx()`/`getFy()` carry the sub-cell pointer position
  (windows always; the terminal in pixel mouse mode 1016).
  `DisplayInfo::key_release`/`precise_mouse`/`hover`/`drag_drop` say what the
  display reports.
- `DragDrop.h`: drag and drop inside the app, where `DisplayInfo::drag_drop`
  (a window and a terminal; the ghost moves by whole cells on a terminal). The source, with the button held, posts
  `StartDrag{this, DragPayload{kind, data}}` to the root (`Handled::no` where
  the display has none, or no button is held: the widget carries on as before;
  `dragStarted()`/`kDragThreshold` say when a press has moved enough). The
  dispatcher then runs the session: the widget under the pointer gets
  `DragOver` on every move and accepts by returning `Handled::yes`, and
  `DragLeave`; the source gets `DragMove` (position, whether the target
  accepts) and `DragEnd{dropped}`, and no `MouseMove`/`MouseUp` meanwhile; a
  release over an accepting widget posts `Drop` to it. The move that started the
  session is its first move (the target hears `DragOver` for it). Escape cancels, and
  the rest of that gesture goes to nobody. `DragGhost` is the plane that
  follows the pointer (modal elevation but no elevated area of its own,
  `setElevationArea(false)`: its raised cells are the lighting and shadow,
  shifted with it; the grabbed point kept under the
  pointer, to the pixel where the display scrolls smoothly, through a
  one-cell margin above and left of the picture, where the slivers of a shifted
  picture show, since a plane clips to itself; the pointer shows
  `MouseCursor::GRABBING` or `NOT_ALLOWED` over it); the source makes it on
  `StartDrag` and drops it on `DragEnd`.
- `ThreadPosts.h`: `ThreadInbox` lets other threads post events to the root,
  dispatched on the UI thread at the start of every loop turn and in the
  turn a post woke; `addRing()` adds a `RealtimeRing` for a producer that
  must never allocate or block (trivially copyable payload <= 64 bytes).
  `Wakeup.h` and the `EventLoop` additions: `wake()` from any thread,
  `onFrame()`/`onWake()` hooks, `run(opener)` with `deleteLater()` for widgets
  given up inside nested runs.

## Planes, scrolling, bands, elevation

- `BufferPlane`/`CellBuffer`: a plane keeps its own cells; drawing, moving,
  raising and hiding work on the plane, and `refresh()` composes the tree
  into a cell grid. A plane's elevation (`Plane::setElevation()`) adds to its
  parent's; the tree is drawn lowest first, equal heights in tree order, so a
  popup floats over everything below wherever it sits. `Theme::elevation`
  steps, in cell widths: panel, raised, card, overlay (menus, pickers,
  popups), modal (dialog + scrim). Cells can stand above their plane
  (`Plane::setLift()`, a cell attribute, so a partial redraw keeps it). Every
  frame reports the areas above the base (`FrameInfo::elevations`).
- `Viewport.h` holds the offset, `reveal()` margins and the wheel's detached
  state every scrolling widget shares. Where `DisplayInfo::smooth_scroll`, it
  animates the shown position toward the offset, and a widget on a plane of
  its own draws one row more and calls `Plane::setScrollOffset()` with the
  fraction; the plane composes as a `BackdropLayer::scroll` layer, which the
  window draws translated from a texture of its own. A plane shifted by part
  of a cell keeps its pictures, proportional text and raised-cell shadows
  (`ElevatedArea::shift_row`/`shift_col`) in the GTK and Win32 windows.
- `Plane::setBand()` puts a translucent strip at a fractional row over a
  plane, unshifted (a playhead, a cursor row). A `Band` can have an inside
  border (`border_px`, rounded by `border_radius`; `bandBorder()` gives the
  pieces both windows draw) and a `BlendMode` (`cellui/BlendMode.h`: the
  separable modes plus hue/saturation/color/luminosity, `blendColors()` on
  encoded sRGB). The window draws it as a GSK blend node cross-faded by alpha;
  a display that cannot scroll by parts of a cell gets the band laid over the
  whole cells it reaches, blending their colours — one primitive, the same
  look everywhere. `Plane::setBlend(mode, alpha)` blends a whole plane the
  same way. `Band::lift` raises a band above shadows so a lower area's shadow
  never falls on a cursor or playhead.
- `Plane::drawSharedImageAt(row, col, ImageKey{kind, id}, w, h, render)`: a picture
  whose pixels depend only on its key; the root keeps the pictures made by key
  (`render` runs only for a key no plane has made; a new cell size or display
  scale, or `invalidateSharedImages()` for a theme, empties it), and the kitty
  encoder uploads one once however many places show it (a placement id each),
  keeps it past its last place up to 8 MB and frees the oldest unplaced.
  Statistics are by the kind (`dial`), or the size for pictures with no key.
- `Plane::drawImageAt()`: a plane holds one image per cell position, covering
  only the cells it reaches; the same picture (by id or pixel hash) drawn
  again, even after `erase()` or at another position, keeps its key and
  version, so it is placed, not re-sent. A widget scrolled partly out of view
  draws its picture whole at its real position with a row clip, so the pixels
  don't change with the scroll.
- `Plane::setCornerRadius()` (`Theme::dialog_radius_px`, 96-dpi pixels, scaled
  by the window) rounds an `ElevatedArea`: shadow outline and redrawn cells
  are rounded, and the cut-away corners show what lay under the corner cell
  (`ElevatedArea::corner_bg`).
- Backdrops are the backend's to apply: `refresh()` sets what is composed
  below each one aside as a `BackdropLayer` (cells, blur, dim, tint,
  rectangle; `FrameInfo::backdrops`, bottom first) and leaves the frame with
  only what is above, so no backend's work shows in `BufferPlane`.

## Text, pictures and the pointer

- `Plane::putText(y, x, cols, text, TextStyle)` gives text a box of `cols`
  cells and lets the display lay it out; the app never measures it. Overflow
  is `CLIP` or `ELLIPSIS`. Weight and slant are not plane state: `putstr()` and
  `putText()` take a `TextFace` (`TextStyle.h`: weight 100-900, default 400,
  and `TextSlant`; `kBoldFace`, `kItalicFace`), and a `Cell` keeps both
  (`Cell::weight`, `italic`), so the windows draw any weight in cell text as in
  a `TextRun` and the terminal writes SGR bold from 600 (`Cell::bold()`). The face also
  carries a `zoom` (`TextFace::zoom`, `Cell::zoom`, 0 to 255 of what the display allows): how much
  larger the text is drawn, which a display scales (the GTK window) or ignores (a terminal). The cells
  of a zoomed text scale as one piece about its common centre: text written in one call is one piece
  (`CellBuffer::write()` marks its cells after the first `Cell::zoom_joined`, as `Cell::lift_joined`
  joins lifted cells), and `TextFace::zoom_joined` joins a call's first cell to the call before it,
  so a field of differently coloured parts (a note with a coloured accidental) is one piece with each
  part keeping its own colour. A display with `DisplayInfo::proportional_text`
  (the GTK window; Win32 with DirectWrite, unrun) marks the box's cells
  (`Cell::text_run`) and reports `FrameInfo::text_runs`, set in the system UI
  font sized to the cell height; any other display, or a `MONOSPACE` request,
  gets the text written as cells (`Plane::cutToCells()`). Writing over a
  marked cell takes the text back.
- The pointer shape is a plane attribute (`Plane::setMouseCursor()`:
  `INHERIT`/`DEFAULT`/`POINTER`/`TEXT`/`GRABBING`/`NOT_ALLOWED`); `refresh()` lists the requesting
  planes' rectangles in drawing order (`FrameInfo::mouse_cursors`) and the
  backend shows the last one under the pointer (GTK by cursor name, the
  terminal with OSC 22, which is why it asks for any-motion mouse reporting).
  A popup sets `DEFAULT` so its area isn't taken for what lies below.
- Charts (`cellui/Charts.h`, `chartsFor(display)` picks the display's): the
  application asks `Charts` for a spectrum, a heatmap, a sparkline or a
  waveform row and the display draws it as it can. Cell versions
  (`term/CellCharts.h`: braille bars, quadrant/sextant heatmap and waveform;
  glyph tables in `term/SubcellGlyphs.h`, `GlyphMeter.h`) are drawn by hand,
  never through canvas, and nothing outside `cellui/term/` knows those
  glyphs. Pixel versions are images drawn with canvas (`LineImage.h`,
  `HeatmapImage.h`, `SparklineImage.h`; `src/PixelCharts.cpp` in the app).
  canvas (`rekola/canvas`: Cairo, Quartz 2D on macOS, GDI+ on Windows) is
  fetched at a pinned commit by `lib/cellui/CMakeLists.txt` with none of our
  warning flags and SYSTEM includes; `cellui::nativeContext()`
  (`src/NativeContext.h`) is the one place that names a canvas backend. An
  `ImageData` handed to canvas is RGB straight-alpha unless tagged
  (`setFormat(ChannelOrder::BGR, true)` for premultiplied B,G,R,A as we build
  them); renderers return RGBA.

## `Backend`, `DisplayInfo` and the event loop

`cellui::Backend` (`cellui/Backend.h`) is all the app needs of a display:
`DisplayInfo` (cell size in pixels, whether pictures can be shown, default
colours, display scale, sextants, `hdr`, size, keymap profile, escape-as-meta,
smooth scroll, proportional text), `BackendEvent`s (`cellui/BackendEvent.h`:
input, resize, paste, close, display changed, focus), a frame to show, and
`setNativeSelection()` (the user selects text with the mouse instead of the
app getting clicks). `TerminalSession`, the GTK and Win32 `Window`s and
`cellui::test::TestBackend` implement it; `InteractiveUI` and its tests use
only `Backend`, `BackendEvent` and `DisplayInfo`. The loop is the backend's
too: `Backend::eventLoop()` gives an `EventLoop` (`run()`, `quit()`,
`iterate(block)`, descriptor sources, a before hook returning how long the
turn may wait, an after hook; re-entrant, so a handler may `run()` a nested
loop for a modal and `quit()` ends the innermost). `PollEventLoop` (poll(2))
serves the terminal and `TestBackend`; `GlibEventLoop` the GTK window;
`Win32EventLoop` waits on the message queue and handles (no descriptors:
`inputFd()`/`wakeFd()` are -1, `inputHandle()`/`wakeHandle()` are events).

`--ui auto|terminal|raster|headless`: `auto` tries the window when built with
GTK and a display is there (`WAYLAND_DISPLAY` or `DISPLAY`), else the
terminal; a named backend that cannot start is an error; the choice is made
once (`cellui/BackendChoice.h`). `SYNTH_WINDOW_STATS=<file>` appends frame
timing (and missed-vsync counts) at exit.

## Terminal backend (`cellui/term/`)

- `InputParser` (keys, mouse, paste, focus, query replies, a short grace for
  split sequences), `Discovery`/`Capabilities` (start-up queries with a
  deadline; true colour is also found by setting a 24-bit background and reading the pen back with DECRQSS, which xterm answers with the colour only when it keeps 24 bits; display scale guessed from the cell size, light/dark from the
  default colours, which picks the default `--theme`), `FrameEncoder` (frame
  diff as escape sequences; colour depth NONE/8/16/256/24-bit, found from `TERM` or `CELLUI_COLORS`; at 16 and 8 colours a colour equal to the terminal's own text or background is sent as the default, and with no colour a background far from the terminal's own is reverse video and a quiet grey text is dim; a foreground lit past white, a channel above `kLitBold`, is written bold
  for its first `kBoldHold` seconds lit (a latch per cell, on a clock a test can set): a terminal's
  way of brighter, so an app never asks a weight for a flash and the bold shows on every lit
  cell for the same short time, whatever the colour does after), `ImageEncoder` (kitty graphics, iTerm2 inline
  images, Sixel; a Sixel image gets its own palette, the picture's exact colours when they fit the registers or their median cut (colours first put on the 101 levels a Sixel colour definition has, so two the terminal cannot tell apart are one register), one Sixel per visible rectangle, and only the changed rows of cells when that is smaller; a picture stretched over its cells (a blurred backdrop) at full quality is instead drawn in the terminal's default 256-colour palette (registers for entries 16-255, ordered dither) where the terminal has 240 registers: about half the bytes and time of a palette made from the picture, in duller colours; `PictureQuality` follows the link class (`pictureQualityFor()`): FULL to 30 Hz, REDUCED (64 colours) at 15 Hz, LOW (16) below, as a palette PNG for kitty and iTerm2 (quantised RGB where a terminal refuses the PNG) and fewer Sixel registers, undithered unless the picture is stretched over its cells (a blurred backdrop); kitty uploads and PNGs deflated by the in-tree
  `term/Deflate.h`, raw fallback when it saves under 10 % or the terminal
  refuses), `TerminalSession` (tty, modes, signals, restoration). `TermEvent`s
  stay inside `term/`; `Capabilities` (image protocol, kitty keyboard, query
  replies, dither) are terminal-only.
- Frames are paced to the link (`FramePacer`; `plans/frame-pacing-terminal.md`
  has the design): the app calls `Backend::requestFrame()` and renders in the
  handler it gave `setFrameHandler()`, called when the pacer lets a frame
  start. Writes go through a non-blocking `OutputQueue` and nothing is encoded
  while it holds bytes. Each frame is led and ended by a fence, `ESC[?6n`, whose
  replies are its acknowledgements; a terminal that answers no cursor query gets a
  fixed 4 fps. The time from the reply to the lead fence to the reply to the end
  fence is the terminal taking in that frame alone (a bandwidth sample when it is
  at least 10 ms, otherwise only a lower bound); the lead fence's own delay is the
  terminal's response delay D, kept over 5 s. Only a saturation-grade sample (the
  bytes in flight when the frame was sent at least 0.75 B*D, using the window's
  median D, and at least `PacerConfig::grade_floor_bytes` (64 kB, capped at 0.75 of 10 ms of data
  at B so a slow link keeps working) so a terminal that answers in 0.2 ms cannot grade small frames) can lower the estimate or back it off; an app-limited one can only raise
  it. `--pacer-log FILE` writes a line per frame; `tools/pacing/real_terminals.py`
  runs the app in VTE, gnome-terminal, kitty and xterm on a virtual display.
- `Backend::linkStatus()` (`cellui/LinkStatus.h`): a `LinkClass` (`FULL`,
  `HZ_30`, `HZ_15`, `HZ_5`, `HZ_1`) from a `LinkClassifier` (down after 2 s, up
  one class after 15 s with a 1.5x margin; pinnable with `--link-class` or
  `set-link-class`) plus a description the backend writes. The terminal
  classifies only on seconds the link held frames back
  (`FramePacer::linkBound()`, which needs the gap the estimate asks for to be
  `link_bound_margin` times the floor between frames: small frames settle at the
  floor rate whatever the link could take), since otherwise the estimate only
  says what the app sent; with the link not limiting the description is a
  rough `~rate` and the round trip, or just `not limiting` when the estimate is
  near the floor rate of the last frame (`PacerStats::floor_rate`); a window reports its monitor's vsync rate and achieved fps.
  `InfoLine` shows `Link: ...` (`n/a` until available). `plans/link-degradation.md`
  plans what an app does at each class. `tools/pacing/run_assertions.py`
  (ctest `cellui_pacing_harness`) runs `cellui_pacing_probe` on a shaped pty.
- The terminal cannot blur: `term::BackdropEmulation` averages cells over
  their neighbours with glyphs faded out, or with pictures draws, blurs and
  stretches the cells in 4x16-cell tiles redrawn only when changed. The link
  class picks its `BackdropMode` (`backdropModeFor()`): live, static (kept as
  when the backdrop appeared, until it goes), cell blur only, or a flat dim.

## GTK window backend (`cellui/gtk/`)

A `GtkWindow` with one custom widget whose `snapshot()` draws the presented
frame per layer in bands of 4 rows, redone only for a band whose part
changed: cell backgrounds as one render node of rectangles per band, text as
Pango glyph nodes (one per glyph, colour and face; a glyph in a face is shaped once and
another colour of it is built from the same glyphs, `recolourText()`; a
cell at a time so glyphs sit on the grid; a glyph the system monospace font
lacks, such as 31/53-EDO accidentals, comes from another font shrunk to its
cell), and pictures as `GdkTexture`s uploaded only when new or re-versioned
and laid over the backgrounds — so a chart changing every frame redoes no
text. The app's descriptors are GSources on GTK's main context; events wait
behind an eventfd that is `inputFd()`. Keys are taken in the capture phase
with real press/repeat/release; the pointer's motion is reported with or
without a button held (a drag), at each new cell; wheel is buttons 4-7; paste is Ctrl+Shift+V / Shift+Insert.
Backdrops: each layer is drawn, then the stack so far again under a GSK blur
node and a dim inside the backdrop's cells — the window never makes the blur
itself. Shadows: two GSK outset shadows (a key light from the upper left,
offset down by half the height, and a faint ambient one) around every
`FrameInfo::elevations` area no later backdrop covers, then height by height
the cells of every area as high or higher again over them, plus a light top
and dark bottom edge per area. Glow: a glyph whose foreground is lit past white
(a `Color` channel above 1, from 0.6 above it at full strength) gets a glow, drawn
over the backgrounds and under the text as a small cached texture: the glyph blurred
tight and wide once on the CPU for a glyph in a face (`glowSprite()`, a white texture
reaching 0.75 cell heights past its cell), then a textured quad each frame, in the glyph's
colour and strength through a colour matrix, so the glow takes any colour with no steps and
no texture of its own (this assumes a GL renderer). No blur runs on the GPU, so a frame
costs what a few hundred small quads do. A glyph is drawn larger by its cell's zoom
(`Cell::zoom`, up to `kZoomMax` more): a transform on its cached node and on its glow's quad,
so no node or texture is made again (the band's cache print holds the zoom), about the centre
of its piece (`zoomRun()`: the cells joined to each other), so its characters keep their places
in it. The zoom is the writer's, not tied to the light. A background never glows. A band with no such
glyph keeps no glow node, and the glow's cache print holds the strengths, since
the cells' own prints are of the colours scaled to white. An app only says how
bright; this window decides that bright means light around it. Not done yet:
per-plane render nodes, input methods, HiDPI, native dialogs and menus.

## Win32 window backend (`cellui/win32/`)

Built on Windows only, compiled in CI but never run. Draws with Direct2D and
DirectWrite: cells, pictures, scrolled planes, backdrop dimming, shadows as
fading rings. Its target has no blend modes or effects, so a blended band or
plane is made by drawing the region again from the cells with every colour
blended (`Window::blendRegion()`; only cells and scrolled planes below show
the blend, not images or blurs), and a blur by drawing the region small and
large again offscreen. It draws cells itself, so `canvasRasterizer()` is null
there. It lets go of held keys and buttons on focus loss and makes a face per
other font weight on demand.

## `TestBackend` and the gallery

`TestBackend` (`cellui/TestBackend.h`) has no tty or window: input is pushed
by the test and each presented frame is kept to read (`text()`, `frame()`
cells and styles, `info()` with the plane tree from `BufferPlane::describe()`
and the focus name, `cursor()`, `images()`, `setHdr()`, `sdrFrame()`,
`emulateBackdrops(true)` to show backdrops as a terminal does). It is named
for the test, not the terminal, so any backend's tests can use it. The
gallery (`Gallery.h`, `cellui_gallery`; `--live` through the display,
`--gtk` in the window) is a golden text file in `lib/cellui/tests/golden/`,
rewritten with `CELLUI_UPDATE_GOLDEN=1`.
