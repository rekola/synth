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
- **Input.** `Input` (key or mouse, press/repeat/release), `KeyChord`, `Keymap`
  and `CommandRegistry` (Emacs-style named commands and prefix sequences, with
  per-display keymap profiles), `KeyReader` (Escape as Meta).
- **Widgets.** `Widget` is the widget base; on top of it `ChoiceDialog`,
  `CommandPalette`, `InfoDialog`, `InlineEditor`, `SpinBox`, `MenuBar`,
  `CoverArt`, and the level, channel and spectrum meters.
- **Backends.** `Backend` is what an application needs of any display
  (`DisplayInfo`, `BackendEvent`s in, frames out). `cellui/term/` is the terminal
  (key and mouse parsing, capability discovery, frame diffing, kitty/iTerm2/Sixel
  pictures), `cellui/gtk/` the window, `cellui/win32/` the Windows window.
- **Loop.** `EventLoop` is the backend's wait-and-dispatch loop; it is
  re-entrant, so a handler may run a nested loop.

## Gallery

`cellui_gallery` prints the gallery page: every text style, the theme's colour
tokens, the level meter ramp, the sub-cell glyphs, width and truncation of
wide and combining characters, a Markdown document, a choice list, the dialogs,
the meters, a spin box and cover art.

    build-cellui/cellui_gallery            # 24-bit colour escape sequences
    build-cellui/cellui_gallery --plain    # the text alone

The page is drawn by `Gallery` through `Plane` only, so any backend can show
it. The text of the page is compared with `tests/golden/gallery.txt`; after a
deliberate change, run the tests with `CELLUI_UPDATE_GOLDEN=1` to rewrite it.

## Events and signals

*Implemented: `Signal` and `Connection` (`cellui/Signal.h`) and posted events on the widget tree (`cellui/EventTypes.h`, `Widget::attach`/`on`/`post`/`postToRoot`), and input events and `InputDispatcher` (`cellui/InputEvents.h`, `cellui/InputDispatcher.h`). The queues and the loop additions are design, and their signatures may change in detail.*

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
  kept separate from the key (empty for a chord with Ctrl, Alt or Meta).
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

Key chords and named commands (`Keymap`, `CommandRegistry`) are a separate layer:
a `KeyDown` handler is where a widget consults its keymap.

### Events from other threads

Other threads never call widgets or emit signals. They post to the root, and the
event is dispatched on the UI thread like an untargeted `post`, with a fresh
`EventContext`. There are two queues because the producers differ. Both are
drained at the start of every loop iteration, nested loops included. An event
whose handlers have all been destroyed is dropped.

**Real-time ring**, for a producer that must not allocate, lock or block (the
audio thread):

```cpp
struct XrunReported {
  int frames;
};  // trivially copyable, at most 64 bytes

if (!ring.post_from_thread(XrunReported{ n })) ++dropped;  // full: the producer decides
```

- A bounded single-producer single-consumer ring of fixed-size slots, one ring
  per real-time producer.
- The payload must be trivially copyable and at most 64 bytes (`static_assert`).
  A slot holds the payload bytes and a plain function pointer that dispatches
  that type.
- It never allocates, locks or blocks, returns `false` when full, and calls
  `wake()` after pushing.

**General queue**, for worker threads, I/O and timers:

```cpp
cellui::post_from_thread(FileLoaded{ path, std::move(contents) });
```

- Multiple producers, one consumer; any movable payload.
- It may allocate and take a short producer-side lock, and never blocks the UI
  thread.
- Unbounded by default; an optional high-water mark makes `post_from_thread`
  return `false`.
- The payload is destroyed on the UI thread, after dispatch.

| | Real-time ring | General queue |
|---|---|---|
| Order within the queue | FIFO | FIFO |
| Order against the other queue | none | none |
| Producer allocates | never | may |
| Producer blocks | never | only for a short lock |
| When full | returns `false`, event not sent | unbounded, or `false` at the high-water mark |
| Delivered exactly once | if `post_from_thread` returned `true` | if `post_from_thread` returned `true` |
| Payload destroyed on | nothing to destroy | the UI thread |
| Delivered | next loop iteration | next loop iteration |

Neither queue promises that a handler still exists when the event is drained, or
any order between events sent through different queues. Events that must stay in
order go through the same queue.

### The loop

- `wake()` can be called from any thread, allocates nothing, and starts a loop
  iteration (an eventfd or self-pipe on Linux, an event handle on Windows). The
  queues call it.
- `on_frame(fn)` registers a function that runs once per iteration before input
  is read, in nested loops too.
- A modal dialog may run a nested loop (`show_modal`); queues, frame hooks and
  redraws keep working inside it. A widget destroyed during a nested loop is
  destroyed when that loop unwinds, and the widget that opened the loop is kept
  alive while it runs.

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
