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

- **Drawing.** `UIPlane` is a rectangle of cells with its own colours, styles,
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
- **Widgets.** `Element` is the widget base; on top of it `ChoiceDialog`,
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

The page is drawn by `Gallery` through `UIPlane` only, so any backend can show
it. The text of the page is compared with `tests/golden/gallery.txt`; after a
deliberate change, run the tests with `CELLUI_UPDATE_GOLDEN=1` to rewrite it.

## Events and signals: a tutorial

> **Status: design.** This section describes the event and signal system cellui
> is moving to. It is not implemented yet. Today input reaches widgets through
> `Element::offerInput(const Input &)`, a widget is told about a change through a
> `std::function` its owner hands it, and the application has its own `Event`
> class hierarchy. Names and signatures below are a sketch and may change in
> detail; the three-mechanism split is the part that is settled.

cellui has three ways for one piece of code to tell another that something
happened. They differ in how much the sender knows about the receiver, and in
which thread they run.

| Mechanism | The sender knows | Runs | Use it for |
|---|---|---|---|
| `Signal<Args...>` | the object it emits from, and the listener holds that object | synchronously, UI thread | a widget telling its owner what changed |
| posted event | nothing about the receiver | dispatched through the widget tree, UI thread | input, focus, shortcuts, app-wide notifications |
| cross-thread event | nothing, and is not on the UI thread | queued, then dispatched like a posted event | audio thread, workers, I/O, timers |

A rule of thumb: if the listener already holds a pointer to the object that
changes, use a signal. If the sender would have to go looking for who cares, or
if more than one unrelated widget may care, post an event.

### Signals

A `Signal` is a member of the object that changes. Whoever holds the object calls
`connect` and keeps the returned `Connection` for as long as the handler should
stay installed.

```cpp
class SpinBox {
 public:
  cellui::Signal<int> value_changed;
  // ...
  void setValue(int v) {
    if (v == value_) return;
    value_ = v;
    value_changed.emit(v);
  }
};

class TempoPanel {
  SpinBox bpm_;
  cellui::Connection bpm_connection_;

 public:
  TempoPanel() {
    bpm_connection_ = bpm_.value_changed.connect([this](int bpm) { controller().setTempo(bpm); });
  }
};
```

What to rely on:

- `Connection` is move-only and disconnects when destroyed, so a handler that
  captures `this` cannot outlive the object that installed it as long as the
  `Connection` is a member of that object. `disconnect()` does it early.
- `emit` is synchronous: the handlers have run when it returns, in the order they
  were connected.
- A handler may disconnect itself or another handler while the signal is
  emitting; the rest of the current emission is still well-defined (a handler
  disconnected before its turn is not called).
- A signal belongs to the UI thread. Do not emit one from another thread; post an
  event instead.
- There is no routing, no return value and no "handled" flag. If you need
  those, you want an event.

Declare the `Connection` after the objects its handler touches, so it is
destroyed first.

### Posted events

An event is any struct. It needs no base class and no registration beyond a
handler, and because it is plain data it can be copied, logged and replayed.

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

// Anywhere on the UI thread, without naming a receiver:
cellui::post(TransportToggled{ true });
```

A handler returns `Handled::yes` to stop the event and `Handled::no` to let it
continue. Everything about the delivery - who sent it, when (the loop's clock),
which phase it is in - is in the `EventContext` passed to the handler. The
context is made on the stack for each dispatch and is never stored with the
event.

There are two ways to post.

**Untargeted: `post(e)`.** The event goes to the root. The root's own handlers
run first, then every widget that registered a handler for that event type, in
tree pre-order (ancestors before descendants, siblings in child order), until one
returns `Handled::yes`. Because a `Handled::no` handler lets the event go on,
several widgets can observe one notification; because order is fixed, which one
wins when several return `yes` is predictable. The cost is proportional to the
number of handlers for that type, not the size of the tree.

**Targeted: `widget.post(e)`.** The event is delivered from that widget, in two
phases:

1. *Capture*, from the root down to the target. An ancestor may intercept here
   (a modal dialog, MIDI-learn) by returning `Handled::yes`, and the target never
   sees the event.
2. *Bubble*, from the target up through its ancestors, until a handler returns
   `Handled::yes`.

`on<E>` takes the phase as an optional second argument; the default is bubble.

```cpp
// A modal dialog swallows every key while it is open, without the widgets
// underneath knowing it exists.
class Dialog : public cellui::Widget {
  cellui::Connection capture_keys_;

 public:
  Dialog() {
    capture_keys_ = on<cellui::KeyDown>(
        [this](const cellui::KeyDown & k, cellui::EventContext &) {
          if (k.key == cellui::Key::Escape) close();
          return cellui::Handled::yes;
        },
        cellui::Phase::capture);
  }
};
```

### Input events

The toolkit's own input is made of ordinary events, using the same mechanism:

- `KeyDown` / `KeyUp`: key code, modifiers, and the UTF-8 text the key produced,
  kept separate from the key.
- `MouseDown` / `MouseMove` / `MouseUp` / `Wheel`: the position in float pixels
  and in integer cells.
- `Enter` / `Leave`, `FocusIn` / `FocusOut`.
- `Resize`: the new size and the cell size in pixels.

Backends turn native input into these and post them. They do not decide who
receives them; the dispatcher does:

- Keyboard events go to the focused widget. The dispatcher owns focus traversal.
- Mouse events go to the topmost widget under the pointer. From `MouseDown` to
  `MouseUp` the pointer is *grabbed*: every event in between goes to the widget
  the button went down on, wherever the pointer is, so a drag cannot be taken
  over by what it passes.
- `Enter` and `Leave` are made from successive hit tests. A widget never works
  out hover for itself.

What a display cannot report is a flag in `DisplayInfo`, not something the
toolkit fakes: for example whether key release is reported, whether the mouse has
pixel precision, whether hover is reported. A widget that needs one checks the
flag and degrades.

```cpp
canvas_.on<cellui::MouseDown>([this](const cellui::MouseDown & m, cellui::EventContext &) {
  begin_stroke(m.pixel_x, m.pixel_y);
  return cellui::Handled::yes;
});
canvas_.on<cellui::MouseMove>([this](const cellui::MouseMove & m, cellui::EventContext &) {
  extend_stroke(m.pixel_x, m.pixel_y);  // arrives for the whole drag, thanks to the grab
  return cellui::Handled::yes;
});
```

Key chords and named commands (`Keymap`, `CommandRegistry`) are unchanged by
this: a `KeyDown` handler is where a widget calls its keymap, as it does from
`offerInput` today.

### Events from other threads

Other threads never call widgets and never emit signals. They post to the root,
and the event is dispatched on the UI thread exactly like an untargeted `post`,
with a fresh `EventContext`. There are two queues, because the producers
differ. Both are drained at the start of every loop iteration, including
iterations of a nested modal loop. If every handler for an event has been
destroyed by the time it is drained, the event is dropped.

**Real-time queue.** For a producer that must not allocate, lock or block (the
audio thread).

```cpp
struct XrunReported {
  int frames;
};  // trivially copyable, at most 64 bytes

// On the audio thread:
if (!ring.post_from_thread(XrunReported{ n })) {
  ++dropped;  // the ring was full; the producer decides what that means
}
```

- A bounded single-producer, single-consumer ring of fixed-size slots, one ring
  per real-time producer.
- The payload must be trivially copyable and at most 64 bytes (a `static_assert`
  checks it). Each slot holds the payload bytes and a plain function pointer
  that knows how to dispatch that type.
- No allocation, no locks, never blocks. It returns `false` when full.
- After pushing it calls `wake()`.

**General queue.** For worker threads, I/O and timers.

```cpp
// On a worker thread:
cellui::post_from_thread(FileLoaded{ path, std::move(contents) });
```

- Multiple producers, one consumer. Any movable payload.
- It may allocate and take a short lock on the producer side, and it never blocks
  the UI thread.
- Unbounded by default; an optional high-water mark makes `post_from_thread`
  return `false` instead.
- The payload is destroyed on the UI thread, after dispatch.

What the queues do and do not promise:

| | Real-time ring | General queue |
|---|---|---|
| Order | FIFO | FIFO per queue |
| Order against the other queue | none | none |
| Allocates on the producer side | never | may |
| Can the producer block | never | only for a short lock |
| When full | returns `false`, event not sent | unbounded, or `false` at the high-water mark |
| Delivered exactly once | if `post_from_thread` returned `true` | if `post_from_thread` returned `true` |
| Payload destroyed on | nothing to destroy | the UI thread |
| Delivery to the UI thread | next loop iteration | next loop iteration |

Neither queue promises *when* within an iteration, that a handler exists when the
event is drained, or any ordering between two events sent through different
queues. If two things must stay in order, send them through the same queue.

### The loop

- `wake()` can be called from any thread, allocates nothing, and makes the loop
  start an iteration (an eventfd or a self-pipe on Linux, an event handle on
  Windows). The queues call it for you.
- `on_frame(fn)` registers a function that runs once per iteration, before input
  is read; it runs in nested loops too.
- A modal dialog may run a nested loop (`show_modal`). Queued events, frame hooks
  and redraws keep working inside it. A widget asked to destroy itself while a
  nested loop is running is destroyed after that loop unwinds, and the widget that
  opened the loop is kept alive for as long as it runs.

### Testing

`TestBackend` has no tty or window: a test pushes events in, steps the loop one
turn at a time and reads what was presented. Routing needs no backend at all;
build a small widget tree, call `post`, and assert the order handlers ran in.

```cpp
TEST(capture_precedes_bubble) {
  std::vector<std::string> order;
  Widget root, panel, button;
  panel.reparent(root);
  button.reparent(panel);
  auto c1 = root.on<KeyDown>([&](auto &, auto &) { order.push_back("root capture"); return Handled::no; }, Phase::capture);
  auto c2 = button.on<KeyDown>([&](auto &, auto &) { order.push_back("button"); return Handled::no; });
  auto c3 = panel.on<KeyDown>([&](auto &, auto &) { order.push_back("panel bubble"); return Handled::no; });
  button.post(KeyDown{ ... });
  CHECK(order == (std::vector<std::string>{ "root capture", "button", "panel bubble" }));
}
```

## Tests

`cellui_tests` holds the unit tests (a copy of the tiny test framework lives in
`lib/cellui/tests/`); `cellui_includes_nothing_from_src` fails if any header or
source reaches into the application.
