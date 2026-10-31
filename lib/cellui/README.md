# cellui

A UI toolkit for applications that draw on a grid of character cells and, where
the display allows, pictures over them. The same widgets run in a terminal, in a
GTK window and (first slice, compiled but not yet run) in a Win32 window; a
`TestBackend` with no tty or window runs them in tests.

It was written for [synth](../../README.md) and lives in this repository, but it
is its own CMake project and includes nothing from the application (`src/`).

    cmake -S lib/cellui -B build-cellui
    cmake --build build-cellui -j
    ctest --test-dir build-cellui --output-on-failure

Only libunistring is needed to build it (the canvas drawing library is fetched
at configure time, GTK is optional). Headers are `cellui/X.h` in
`namespace cellui`.

## What is in it

- **Drawing.** `Plane` is a rectangle of cells with its own colours, styles,
  elevation and optional pictures; planes form a tree. `BufferPlane` keeps its
  own cells and `refresh()` composes the tree into a `CellBuffer`, which renders
  as plain text or ANSI. Backdrops (blur, dim, tint), shadows, smooth scrolling
  and pointer shapes are plane attributes that each backend realises as it can.
- **Text and colour.** `Color` (linear Rec.2020 floats, so HDR is possible),
  `utf8` (grapheme-aware width, truncation and padding), the sub-cell glyph
  tables, `markdown` (a tiny subset for info boxes).
- **Input.** `Input` (key or mouse, press/repeat/release), `KeyStroke`, `Keymap`
  and `CommandRegistry` (Emacs-style named commands and prefix sequences, with
  per-display keymap profiles), `KeyReader` (Escape as Meta).
- **Widgets.** `Widget` is the widget base; on top of it `ChoiceDialog`,
  `CommandPalette`, `InfoDialog`, `InlineEditor`, `SpinBox`, `MenuBar`,
  and the line chart.
- **Backends.** `Backend` is what an application needs of any display
  (`DisplayInfo`, `BackendEvent`s in, frames out). `cellui/term/` is the terminal
  (key and mouse parsing, capability discovery, frame diffing, kitty/iTerm2/Sixel
  pictures), `cellui/gtk/` the window, `cellui/win32/` the Windows window.
- **Loop.** `EventLoop` is the backend's wait-and-dispatch loop; it is
  re-entrant, so a handler may run a nested loop.

## Why cells

What the cell approach buys is still being found out. This lists what the code
and the measurements so far support, and then what is only a hope.

**Shown**

- **One widget set on every display.** The widgets draw into planes of cells and
  never name a display. The terminal, the GTK window, the Win32 window and the
  test backend all run the same ones. The application asks for what it wants
  (a spectrum, a sparkline, a clip name in a box of 12 cells) and each display
  draws it as it can: pictures where it has them, glyphs where it has not.
  Nothing outside `term/` knows a braille or block glyph.
- **Redrawing only what changed.** A frame is a grid of cells, so a window can
  cache by region. The GTK window keeps each layer's cells in bands of four
  rows and redoes a band's backgrounds or text only when a cell in it changed;
  a picture is uploaded once and only placed again; a glyph is shaped once for
  its character, colour and face, and every cell that shows it shares the
  render node. A chart that changes every frame redoes no text. On synth's main
  screen this took the GTK window from about 6.9 ms to about 1.3 ms of drawing
  per frame, and from about 6.5 ms to well under 0.1 ms of rasterising.
- **Layout the display owns.** The application gives a string and a box in
  cells. The display measures, cuts or ellipsizes it (`TextOverflow`) and picks
  the face, so no application code measures text and a terminal falls back to
  plain cells without the application changing.
- **Effects as plane attributes.** Elevation, shadows, blur, dim, blend modes,
  smooth scrolling and pointer shapes are set on planes. A window realises them
  with its own graphics; the terminal emulates what it can from cells. One
  description gives a close look on both.
- **Tests without a display.** `TestBackend` has no tty or window, so the real
  application runs in a test: keys go in, and the frame's cells, styles,
  pictures and plane tree come out. The gallery is a golden file.
- **Cell-based input.** Hit areas, focus and the mouse are in cells (with the
  sub-cell position where the display reports it), the same for a click in a
  terminal and in a window.

**Not shown yet**

- Whether the cell grid is faster or simpler than a conventional toolkit for
  the same screen. No such comparison has been made; the figures above compare
  cellui with an earlier version of itself.
- Win32 has never been run, and HiDPI, input methods and native dialogs and
  menus are not done.
- Text in a face of its own works only through `Plane::putText()`; everything
  else is monospace on the grid.

**Costs**

- Everything snaps to cells. A proportional run has a box a whole number of
  cells wide, and smooth scrolling and bands are the way round it, not the
  default.
- A window backend has to realise each plane attribute itself, and the terminal
  can only approximate several of them.

## Gallery

`cellui_gallery` prints the gallery page: every text style, the theme's colour
tokens, the level meter ramp, proportional text, width and truncation of
wide and combining characters, a Markdown document, a choice list, the dialogs,
the meters, and a spin box.

    build-cellui/cellui_gallery            # 24-bit colour escape sequences
    build-cellui/cellui_gallery --plain    # the text alone
    build-cellui/cellui_gallery --live     # full screen in the terminal; charts are pictures where it shows them
    build-cellui/cellui_gallery --gtk      # in a GTK window (when built with GTK), charts as pictures

The page is drawn by `Gallery` through `Plane` only, so any backend can show
it. The text of the page is compared with `tests/golden/gallery.txt`; after a
deliberate change, run the tests with `CELLUI_UPDATE_GOLDEN=1` to rewrite it.

## Adapting to a slow link

*Design; not built yet.* The terminal backend measures its link (bandwidth and round
trip; see `cellui/term/FramePacer.h`), and a window knows its vsync rate and how many
frames it really gets. Each backend turns that into a **link status**:

- a **class**, `FULL` (60 Hz and up), `HZ_30`, `HZ_15`, `HZ_5` or `HZ_1`, for what the
  link sustains; the application listens for changes and, in the slow classes, stops
  what it cannot afford: scopes first, then animations, hover effects and smooth
  scrolling (the pattern editor then jumps a page at a time);
- a **description**, a string the backend writes for its own kind of link, shown
  as is in the application's status bar: `480 kB/s, RTT 38 ms` for a terminal,
  `60 Hz vsync, 58 fps` for a window.

A class can be pinned by the user and is otherwise measured, quickly down and slowly up.

Colours stay on every terminal that has any: an 8- or 16-colour terminal gets a theme made
of its own palette's hues (so track colours stay apart), and there is no automatic grey. A
terminal with no colour at all gets `MONO`, with reverse video for highlights. A slow link
lowers **picture quality** instead, because pictures are rectangles that can have fewer colours
without a seam: palette pictures in the kitty and iTerm2 protocols, a palette made from the
picture's own colours in Sixel (not a fixed 240-colour cube), compressed by a small deflate
encoder in the library, with a Sixel image sent in its changed tiles and not its bounding box.

What the classes never touch: dials stay pictures (they are mostly still, and the
terminal keeps them after the first upload; a widget gives a picture a key and shares one upload
across every place it is shown), and an application's own widgets decide what a class means
for them.

## Events and signals

*Implemented: `Signal` and `Connection` (`cellui/Signal.h`) and posted events on the widget tree (`cellui/EventTypes.h`, `Widget::attach`/`on`/`post`/`postToRoot`), input events and `InputDispatcher` (`cellui/InputEvents.h`, `cellui/InputDispatcher.h`), the cross-thread queues (`cellui/ThreadPosts.h`) and the loop additions (`cellui/EventLoop.h`).*

cellui has three ways to tell other code that something happened. They differ
in how much the sender knows about the receiver and in which thread they run.

| Mechanism | The sender knows | Runs | Use it for |
|---|---|---|---|
| `Signal<Args...>` | the object it emits from; the listener holds that object | synchronously, UI thread | a widget telling its owner what changed |
| posted event | nothing about the receiver | dispatched through the widget tree, UI thread | input, focus, shortcuts, app-wide notifications |
| cross-thread event | nothing, and is not on the UI thread | queued, then dispatched like a posted event | the audio thread, workers, I/O, timers |

If the listener already holds the object that changes, use a signal. If the
sender would have to look for who cares, or several unrelated widgets may care,
post an event.

### Signals

A `Signal` is a member of the object that changes. The holder of that object
calls `connect` and keeps the returned `Connection` for as long as the handler
should stay installed.

```cpp
class SpinBox {
 public:
  cellui::Signal<int> value_changed;

  void setValue(int v) {
    if (v == value_) return;
    value_ = v;
    value_changed.emit(v);
  }
  // ...
};

class TempoPanel {
  SpinBox bpm_;
  cellui::Connection bpm_connection_;

 public:
  TempoPanel() {
    bpm_connection_ = bpm_.value_changed.connect([this](int bpm) { setTempo(bpm); });
  }
};
```

- `Connection` is move-only and disconnects when destroyed; `disconnect()` does
  it early. A handler that captures `this` is safe as long as its `Connection` is
  a member of that object, declared after whatever the handler touches.
- `emit` is synchronous. Handlers run in the order they were connected.
- A handler may disconnect itself or others during an emission; one
  disconnected before its turn is not called.
- A handler connected during an emission is first called by the next one. A handler may also emit the signal again or destroy the object that owns it.
- A `Connection` outlives its signal safely. A `Signal` is not movable, since connections name it.
- Signals belong to the UI thread. Other threads post events.
- There is no routing, return value or "handled" flag. For those, use an event.

### Posted events

An event is any struct: no base class, no registration. Because it is plain
data it can be copied, logged and replayed.

```cpp
struct TransportToggled {
  bool playing;
};

class TransportBar : public cellui::Widget {
  cellui::Connection on_toggle_;

 public:
  TransportBar() {
    on_toggle_ = on<TransportToggled>([this](const TransportToggled & e, cellui::EventContext &) {
      setIcon(e.playing ? "▶" : "■");
      return cellui::Handled::no;  // let other widgets see it too
    });
  }
};

any_widget.postToRoot(TransportToggled{ true });  // from any widget of the tree, on the UI thread
```

A handler returns `Handled::yes` to stop the event and `Handled::no` to let it
continue. The `EventContext` passed to a handler carries the source widget, the
loop-clock timestamp, the phase and an accepted flag. It is made on the stack
for each dispatch and never stored with the event.

**Untargeted: `widget.postToRoot(e)`.** The event goes to the root of the
widget's tree. The root's handlers run first, then every widget with a
handler for that type, in tree pre-order
(ancestors before descendants, siblings in child order), until one returns
`Handled::yes`. Handlers that return `Handled::no` let several widgets observe
one notification. Cost is proportional to the number of handlers for the type,
not the size of the tree. Capture and bubble handlers alike run, with the
context's phase set to bubble.

**Targeted: `widget.post(e)`.** Delivery has two phases:

1. *Capture*, from the root down to the target. An ancestor may intercept by
   returning `Handled::yes` (a modal dialog, MIDI-learn); the target never sees
   the event.
2. *Bubble*, from the target up through its ancestors, until a handler returns
   `Handled::yes`.

`on<E>` takes the phase as an optional second argument; the default is bubble.
Both `post` and `postToRoot` return whether some handler took the event.

The tree is links only: `child.attach(parent)` makes a widget the last child of
another (leaving its old parent), `detach()` makes it a root, and a widget that
is destroyed leaves its children as roots. The order of a root post follows the
tree as it is when the event is posted, however handlers were installed, moved
or removed before.

```cpp
// While open, a dialog takes every key before the widgets under it.
capture_keys_ = on<cellui::KeyDown>(
    [this](const cellui::KeyDown & k, cellui::EventContext &) {
      if (k.key == cellui::Key::Escape) close();
      return cellui::Handled::yes;
    },
    cellui::Phase::capture);
```

### Input events

The toolkit's input is ordinary events (`cellui/InputEvents.h`):

- `KeyDown` / `KeyUp`: key id, modifiers, and the UTF-8 text the key produced,
  kept separate from the key (empty for a shortcut with Ctrl, Alt or Meta).
- `MouseDown` / `MouseMove` / `MouseUp` / `Wheel`: position in cells (fractional
  where the display is precise) and the cell itself as `row`/`col`.
- `Enter` / `Leave`, `FocusIn` / `FocusOut`.
- `Resize`: the new size in cells.

`InputDispatcher` (`cellui/InputDispatcher.h`) is built over a root widget and
chooses the target:

- Keyboard events go to the focused widget, or the root when nothing has focus.
  `focusNext()` / `focusPrev()` walk the widgets marked `setFocusable(true)` in
  tree order, and a press focuses the nearest focusable widget it lands on.
- Mouse events go to the topmost widget under the pointer. The dispatcher finds
  it in the frame's hit areas: `BufferPlane::takeHitAreas()` lists the area of
  every plane that has an owning widget, lowest first, and `setHitAreas()` hands
  them over after each `refresh()`. From the first `MouseDown` to the last
  `MouseUp` the pointer is grabbed: every event in between goes to the widget the
  button went down on, wherever the pointer is.
- `Enter` and `Leave` come from successive hit tests (or the grabbing widget):
  `Leave` for what the pointer left, deepest first, then `Enter` for what it
  came onto, outermost first. They go to that widget only (`Widget::postDirect()`),
  never to its ancestors. Widgets never work out hover themselves.
- `offer(const Input &)` translates the backends' `Input` into these events.

The dispatcher holds widgets only through their `Widget::life()` token, so a
widget destroyed during a gesture, or by a handler, is skipped.

What a display cannot report is a flag in `DisplayInfo`, never faked:
`key_release`, `precise_mouse` and `hover`. A precise position reaches the
dispatcher as `Input::getFx()` / `getFy()` (cells with the fraction; the whole
cell where the display is not precise). The window backends report it always
and send a hover `Key::Motion` for every position change; the terminal reports
it while it has pixel mouse reporting on (mode 1016, once the cell size is known).

```cpp
canvas_.on<cellui::MouseDown>([this](const cellui::MouseDown & m, cellui::EventContext &) {
  begin_stroke(m.x, m.y);
  return cellui::Handled::yes;
});
canvas_.on<cellui::MouseMove>([this](const cellui::MouseMove & m, cellui::EventContext &) {
  extend_stroke(m.x, m.y);  // arrives for the whole drag
  return cellui::Handled::yes;
});
```

Key shortcuts and named commands (`Keymap`, `CommandRegistry`) are a separate layer:
a `KeyDown` handler is where a widget consults its keymap.

### Events from other threads

Other threads never call widgets or emit signals. They post to a `ThreadInbox`
(`cellui/ThreadPosts.h`) made over the loop and the root widget, and the event
is dispatched on the UI thread like an untargeted `postToRoot`, with a fresh
`EventContext`. There are two queues because the producers differ. Both are
drained at the start of every loop turn, nested runs included, and in the turn
a post woke the loop (so the turn's after hook sees what they changed). An
event whose handlers have all been destroyed is dropped.

**Real-time ring** (`RealtimeRing`), for a producer that must not allocate,
lock or block (the audio thread):

```cpp
cellui::ThreadInbox inbox(loop, root);
cellui::RealtimeRing & ring = inbox.addRing(256);  // UI thread, before the producer starts

struct XrunReported {
  int frames;
};  // trivially copyable, at most 64 bytes

if (!ring.post(XrunReported{ n })) /* full: the producer decides; ring.dropped() counts */;
```

- A bounded single-producer single-consumer ring of fixed-size slots, one ring
  per real-time producer; the size is rounded up to a power of two.
- The payload must be trivially copyable and at most 64 bytes (`static_assert`).
  A slot holds the payload bytes and a plain function pointer that dispatches
  that type.
- It never allocates, locks or blocks, returns `false` when full, and calls
  `EventLoop::wake()` after pushing.

**General queue** (`ThreadInbox::post`), for worker threads, I/O and timers:

```cpp
inbox.post(FileLoaded{ path, std::move(contents) });
```

- Multiple producers, one consumer; any movable payload.
- It may allocate and takes a short producer-side lock, and never blocks the UI
  thread.
- Unbounded by default; `setHighWater(n)` makes `post` return `false` while `n`
  events wait.
- The payload is destroyed on the UI thread, after dispatch.

| | Real-time ring | General queue |
|---|---|---|
| Order within the queue | FIFO | FIFO |
| Order against the other queue | none | none |
| Producer allocates | never | may |
| Producer blocks | never | only for a short lock |
| When full | returns `false`, event not sent | unbounded, or `false` at the high-water mark |
| Delivered exactly once | if `post` returned `true` | if `post` returned `true` |
| Payload destroyed on | nothing to destroy | the UI thread |
| Delivered | the turn it wakes, or the next | the turn it wakes, or the next |

Neither queue promises that a handler still exists when the event is drained, or
any order between events sent through different queues. Events that must stay in
order go through the same queue. The inbox and the root must outlive the
producers.

### The loop

- `wake()` can be called from any thread, allocates nothing, and ends the wait of
  the turn in progress or starts the next (an eventfd, or a pipe where there is
  none, on POSIX; an event handle on Windows). A wake sent before the loop
  waits is not lost. The queues call it.
- `onFrame(fn)` registers a function that runs at the start of every turn,
  before the turn waits, in nested runs too; `onWake(fn)` one that runs in the
  turn a `wake()` ended the wait of, before the other sources. Both return a
  `Connection`.
- A modal dialog may run a nested loop (`run(opener)`); queues, frame hooks and
  redraws keep working inside it. `deleteLater(std::unique_ptr<T>)` is how a
  handler gives up a widget it, or a handler further out, may still be using:
  it is destroyed when the turn it was given up in is over, and, if it is the
  `opener` of a `run()` still active, only once that run has ended and the turn
  that called it too. Outside any `run()` it is destroyed at once.

### Testing

`TestBackend` has no tty or window: a test pushes events in, steps the loop one
turn and reads what was presented. Routing needs no backend; build a small widget
tree, call `post`, and assert the order handlers ran in.

```cpp
TEST(capture_precedes_bubble) {
  std::vector<std::string> order;
  Widget root, panel, button;
  panel.attach(root);
  button.attach(panel);
  auto a = root.on<KeyDown>([&](auto &, auto &) { order.push_back("root capture"); return Handled::no; }, Phase::capture);
  auto b = button.on<KeyDown>([&](auto &, auto &) { order.push_back("button"); return Handled::no; });
  auto c = panel.on<KeyDown>([&](auto &, auto &) { order.push_back("panel bubble"); return Handled::no; });
  button.post(KeyDown{ /* ... */ });
  CHECK(order == (std::vector<std::string>{ "root capture", "button", "panel bubble" }));
}
```

## Tests

`cellui_tests` holds the unit tests (a copy of the tiny test framework lives in
`lib/cellui/tests/`); `cellui_includes_nothing_from_src` fails if any header or
source reaches into the application.
