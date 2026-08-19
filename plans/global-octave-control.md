# A prominent global octave stepper, with per-Launchpad octave relative to it

## Context

There is currently no single "the current octave" concept - there are two
independent ones, neither visible anywhere on screen:

- `PatternEditor::current_keyboard_octave` (`PatternEditor.h:134`, default 4) -
  adjusted by raw `[`/`]` key handling (`PatternEditor.cpp:1206-1213`, only
  reachable while `PatternEditor` itself has input focus) and fed straight
  into `InputEvent::toMidiNote(octave, tuning)` (`PatternEditor.cpp:1389`) for
  computer-keyboard note entry. Not a named command - one of the raw,
  not-yet-migrated key handlers CLAUDE.md's keybinding-dispatch section
  tracks as still outstanding.
- `LaunchpadManager::DeviceState::octave` (`LaunchpadManager.h:294`, default
  4, clamped to `[0, 9]` via `LaunchpadLayout::clampOctave`) - a fully
  independent, per-physical-device absolute octave, adjusted by that
  device's own hardware Octave Up/Down buttons (`octaveUp()`/`octaveDown()`,
  `LaunchpadManager.cpp:415-430`, reached through `handleCommand()`'s
  `"octave-up"`/`"octave-down"` strings) and read by `resolveNote()`
  (`LaunchpadManager.cpp:729`) and `refreshLeds()`
  (`LaunchpadManager.cpp:1248`, for LED coloring).

Neither is persisted (not in song XML, not anywhere in `Controller`) - both
are ephemeral session/editing state, the same category `Controller::
recording_track_id`/`pending_command_track_` already live in.

Renoise's own top toolbar has almost exactly the widget asked for here - an
"Oct" numeric spinner with click-to-step arrows and a directly-editable
value field - which is the precedent CLAUDE.md asks to check for UI
decisions like this one.

There's no existing precedent in this codebase for a widget doing its own
sub-region mouse hit-testing (button A vs. field vs. button B within one
small widget) - the only real mouse-click precedent is `TerminalMenu`'s
whole-item click (`TerminalUI.cpp:548-566`, resolved via `ncmenu`'s own
`get_mouse_selected()`), and `PatternEditor`'s own `NCKEY_BUTTON1` handler
(`PatternEditor.cpp:1115`) is currently an empty stub. This plan's `SpinBox`
is the first widget to actually do this, using plain column-range
comparisons against `input.getY()`/`getX()` (absolute screen coordinates,
per `InputEvent.h`) minus the widget's own `getPosition()`.

## Design

### 1. Single source of truth: `Controller`

Add to `Controller` (private, alongside `pending_command_track_`/
`recording_track_id` - ephemeral editing-session state, never serialized):

```cpp
int global_octave_ = 4;
```

```cpp
int getGlobalOctave() const { return global_octave_; }
void setGlobalOctave(int v);   // clamps into [kMinOctave, kMaxOctave]
void octaveUp();               // ±1, same clamp
void octaveDown();
```

`setGlobalOctave()` is what the `SpinBox`'s typed-entry commit path calls;
`octaveUp()`/`octaveDown()` back both the `[`/`]` keyboard shortcut and the
stepper's own `[-]`/`[+]` buttons - one underlying value, three ways to
reach it, so it can never drift out of sync with what's on screen.

Add `constexpr int kMinOctave = 0, kMaxOctave = 9;` to `src/util/
constants.h` - the single named source for the bound that's currently a
bare `0`/`9` literal pair inside `LaunchpadLayout::clampOctave`
(`LaunchpadLayout.cpp:352-357`) and would otherwise get silently
re-duplicated a second time in `Controller`. Update `clampOctave` to use
them too.

### 2. `PatternEditor`: keyboard note entry reads through `Controller`; `[`/`]` become named `UI` commands

- Delete `current_keyboard_octave` and the raw `[`/`]` handling block.
- `toMidiNote(current_keyboard_octave, tuning)` becomes
  `toMidiNote(getController().getGlobalOctave(), tuning)`.
- Register `octave-up`/`octave-down` as named commands on `UI` itself (not
  `PatternEditor`) bound to `[`/`]` in `UI`'s own `keymap_`/`commands_` -
  global, works regardless of which widget currently has focus, the same
  way `quit`/`new-song`/`toggle-playing` already are (CLAUDE.md's
  keybinding-dispatch section). Bodies are one-liners:
  `getController().octaveUp()`/`octaveDown()`. This both finishes
  migrating the last of `PatternEditor`'s raw note-entry-adjacent key
  handling into the centralized dispatch system, and is an intentional
  widening of scope: today `[`/`]` only fire while `PatternEditor` is the
  active element; as global commands they'll work no matter what's
  focused, matching "controls the octave for everything."
- The `dispatchCommand()` reader-guard at the top of `UI::offerInput()`
  (currently `!status_line_->isReaderActive() && !pattern_editor_->
  isReaderActive()`) gains a third condition, `!octave_control_->
  isEditing()` (§4) - so `[`/`]` can't fire *through* a half-typed value in
  the new stepper's own text field, the same reasoning that already
  excludes the other two readers.

### 3. `LaunchpadManager`: per-device octave becomes an offset

- Rename `DeviceState::octave` (absolute, default 4) to `DeviceState::
  octave_offset` (relative to the global octave, default 0 = "follow it
  exactly").
- New member `int cached_global_octave_ = 4;`, mirrored once per frame at
  the top of `refresh()` (`cached_global_octave_ = controller.
  getGlobalOctave();`) - the exact same "mirror a `Controller`-owned value
  into a member once per `refresh()` call" pattern already used for
  `capture_enabled_` (see `LaunchpadManager.h`'s "Record-arm state,
  mirrored here..." comment), rather than threading `Controller &` into
  every call site that currently doesn't have one (`octave()` itself is
  `const` and has none).
- `octave(int device_id) const` becomes:
  ```cpp
  int
  LaunchpadManager::octave(int device_id) const {
    auto * state = findDeviceState(device_id);
    auto offset = state ? state->octave_offset : 0;
    return LaunchpadLayout::clampOctave(cached_global_octave_, offset);
  }
  ```
  reusing `clampOctave()` completely unmodified - "octave + delta, clamped
  to `[kMinOctave, kMaxOctave]`" is exactly what "global + offset, clamped"
  already is.
- `octaveUp(device_id)`/`octaveDown(device_id)` (the physical hardware
  buttons, reached via `handleCommand()`'s existing `"octave-up"`/
  `"octave-down"` strings - untouched, no naming collision with §2's new
  `UI`-level commands, since `LaunchpadManager::handleCommand()` and `UI`'s
  `commands_` are two entirely separate dispatch tables) now adjust
  `state.octave_offset` by ±1. The *combined* value is already clamped at
  read time above, but the raw offset itself still needs its own bound, or
  it could drift arbitrarily far while the global octave sits at an
  extreme. Add a sibling pure function:
  ```cpp
  // Clamps a per-device octave *offset* (relative to whatever the global
  // octave currently is) by delta (+1/-1) to [-(kMaxOctave-kMinOctave),
  // kMaxOctave-kMinOctave] - the offset's own bound, independent of
  // clampOctave()'s combined-value bound above.
  int clampOctaveOffset(int offset, int delta);
  ```
  in `LaunchpadLayout.h`/`.cpp`, with its own unit tests in
  `tests/LaunchpadLayoutTests.cpp` next to the existing `clampOctave`
  coverage.
- `defaultOctaveOffsetForModel()` (the connect-time per-model nudge,
  `LaunchpadManager.cpp:337`, applied via `LaunchpadLayout::clampOctave` at
  `LaunchpadManager.cpp:1463`) keeps its exact call shape - only the
  function it calls (`clampOctaveOffset` instead of `clampOctave`) and the
  field it writes (`octave_offset` instead of `octave`) change. Its
  *meaning* does change, though, and that's the actual point of this plan:
  today every device without a model-specific nudge starts at absolute
  octave 4 regardless of anything else; after this, every device starts at
  offset 0 - i.e. *exactly* the current global octave - and a model's
  nudge shifts it slightly up/down from there instead of from a fixed
  absolute baseline.
- `refreshLeds(int device_id, DeviceState & state)`'s own direct `state.
  octave` read (`LaunchpadManager.cpp:1248`, computing `base_note` for LED
  coloring) changes to `octave(device_id)` - it already has `device_id` in
  scope, so this is a one-line change that keeps LED coloring and note
  resolution reading the same combined value instead of one of them
  quietly reading the raw, uncombined offset.
- `resolveNote()`'s existing `octave(device_id)` call site
  (`LaunchpadManager.cpp:729`) needs no change - it already goes through
  the accessor being updated above.

### 4. Reusable `SpinBox` component

**Revised after first implementation**: the original design below (a
bordered box in the scope strip, `[-]`/`[+]` as 3-character bracketed
buttons) shipped and turned out too big and too wide-buttoned in practice.
`SpinBox` is now borderless and single-row, its buttons a single character
cell each (`-`/`+`, set apart from the label/value only by
`styles.highlight_bg_color`, not by brackets or surrounding whitespace),
and it lives inline in `InfoLine`'s own row instead of the scope strip
(`UI::layout()` sizes it via `preferredWidth()` and right-aligns it there,
as a separate plane created after `info_line_` so it z-orders above
whatever `info_line_` put underneath it - see `UI.cpp`'s own comment at
that call site). The `bg_color`/`fg_color` constructor parameters (new)
let it match whatever bar it's embedded in - `UI::initialize()` passes
`InfoLine`'s own hardcoded gray-on-dark. Everything below this note
describes the box/scope-strip version as originally designed; it's
superseded by the above, kept for the reasoning that still applies
(single source of truth in `Controller`, offset-based Launchpad octave,
click-on-release/scroll-wheel/click-to-select-and-type interaction, the
focus-loss-cancels-edit contract).

New header-only `src/ui/SpinBox.h` (same header-only-`UIElement`
convention as `InfoLine.h`/`Chart.h`). It never touches `Controller`
itself - only through get/set callbacks its owner supplies - so it's
genuinely reusable for any other bounded integer later (a natural second
user: `PatternEditor::edit_step_size`, currently only reachable via
Ctrl+/Ctrl-).

```cpp
class SpinBox : public UIElement {
 public:
  SpinBox(UIPlane & parent, std::string label, int min_value, int max_value,
          std::function<int()> get_value, std::function<void(int)> set_value);

  bool render(const StyleProvider & styles, bool refresh = false);
  bool offerInput(const InputEvent & input) override;
  bool isEditing() const { return editing_; }

 private:
  std::string label_;
  int min_value_, max_value_;
  std::function<int()> get_value_;
  std::function<void(int)> set_value_;

  bool editing_ = false;
  std::string edit_buffer_;

  // Dirty-check cache, same convention InfoLine::current_*_ already uses.
  int current_value_ = 0;
  bool current_editing_ = false;
  std::string current_edit_buffer_;
};
```

Rendered as a bordered box (`UIPlane::drawBorder()` - the same primitive
`windows_`-style bordered widgets like `HierarchyView` already use for
this "visually set apart" look), one content row, e.g.:

```
┌─────────────────┐
│ Octave [-] 4 [+] │
└─────────────────┘
```

`offerInput()` hit-testing, on `NCKEY_BUTTON1` with `Kind::RELEASE`
(matching `TerminalMenu`'s own click-completes-on-release convention,
`TerminalUI.cpp:562` - a mouse-down that drags off a button before release
shouldn't fire it): translate the click to a column relative to
`getPosition()`, then compare against the three interactive spans (`[-]`,
the value field, `[+]`), computed by a small private `layoutColumns()`
helper shared with `render()` so the two can never drift apart.

- `[-]`/`[+]`: `set_value_(std::clamp(get_value_() -/+ 1, min_value_,
  max_value_))`. Belt-and-suspenders clamp - `SpinBox` never assumes its
  setter already clamps, even though `Controller::setGlobalOctave()` does.
- The value field: enters edit mode (`editing_ = true; edit_buffer_.
  clear();`). Rendered afterward with the field's colors inverted over the
  *existing* value - the same foreground/background swap `PatternEditor`'s
  own region highlight already uses for "this is selected" (CLAUDE.md's
  selection-highlight section) - so typing a digit immediately replaces
  the old value rather than requiring it to be cleared first. This is what
  the task's "text input can also be selected using a mouse and octave
  written" means in practice for a field this small: click selects the
  whole (only) value, type to overwrite it.
- `NCKEY_BUTTON4`/`5` (scroll wheel) over the widget step it the same as
  `[-]`/`[+]`, matching `PatternEditor`'s existing row-scroll-wheel
  convention (`PatternEditor.cpp:1250-1256`).

While `editing_`: plain digit keys append to `edit_buffer_` (capped at 2
characters - never more than a 2-digit octave), `NCKEY_BACKSPACE` trims it,
`NCKEY_ENTER` commits (`set_value_(clamp(parse(edit_buffer_)))`, or leaves
the value untouched if `edit_buffer_` is still empty - clicking the field
then immediately hitting Enter is a no-op, not a reset-to-zero),
`NCKEY_ESC` cancels without committing. Losing focus (some other click
lands elsewhere, so `UI::offerInput()` moves `active_element_` away) is
treated the same as `NCKEY_ESC` - cancel silently rather than commit-on-
blur, matching `StatusLine`'s own explicit-commit-or-cancel reader instead
of inventing a third behavior for this one field.

`render()` follows `InfoLine`'s dirty-check convention (compare against
`current_value_`/`current_editing_`/`current_edit_buffer_`, redraw and
update the cache only on a real change or `refresh == true`).

### 5. Wiring into `UI`

New member, constructed in `UI::initialize()` next to `info_line_`/
`status_line_`:

```cpp
std::shared_ptr<SpinBox> octave_control_;
...
octave_control_ = make_shared<SpinBox>(getPlane(), "Octave", kMinOctave, kMaxOctave,
  [this] { return getController().getGlobalOctave(); },
  [this](int v) { getController().setGlobalOctave(v); });
```

`UI::layout()`: carve a new fixed-width column out of the existing scope
strip (`kScopeRow`/`kScopeHeight`, currently chart/heatmap/volume-meter
side by side with single-column dividers, `UI.cpp:300-316`) - this row is
always visible regardless of what has focus, which is what makes it
"prominent" rather than tucked into `InfoLine`'s already-dense single text
line. Placed leftmost, as the first thing seen:

```cpp
constexpr int kOctaveWidth = 19; // "┌───...───┐" / " Octave [-] 4 [+] " - refine once rendered
int chart_width = cols - kOctaveWidth - 1 - 9 - kHeatmapWidth - 2;
octave_control_->resize(kScopeHeight, kOctaveWidth).move(kScopeRow, 0);
chart_->resize(kScopeHeight, chart_width).move(kScopeRow, kOctaveWidth + 1);
...
```

plus one more `putstr(kScopeRow + row, kOctaveWidth, "│")` divider column,
the same loop the existing two dividers already use.

`UI::offerInput()`'s `NCKEY_BUTTON1` branch adds `octave_control_` as a
`tryActivate()` candidate, alongside `pattern_editor_` and `windows_`
(`UI.cpp:400-414`). A click on it both focuses it *and* - the very same
input event is delivered again to `active_element_.lock()->offerInput(
input)` a few lines later in the same function - is immediately handled as
the actual button/field click. No extra plumbing: this is exactly the
two-phase dispatch every other click-activatable widget already goes
through.

## Testing

- `tests/LaunchpadLayoutTests.cpp`: new cases for `clampOctaveOffset`
  (mirroring the existing `clampOctave` cases - clamps at both bounds,
  no-op mid-range).
- No render/audio-path coverage needed - this is pure UI/session state
  with no effect on `--render` output, and `synth_tests` doesn't build the
  notcurses UI at all (CLAUDE.md's Tests section), so `SpinBox`/`UI`
  changes aren't unit-testable there anyway - the same reason `PatternEditor`/
  `StatusLine`'s own mouse-driven bits have no unit tests today; only
  `LaunchpadLayout`'s pure-function core does.
- Manual verification: run the real app; click `[-]`/`[+]`; click the
  field, type a value, press Enter; confirm `[`/`]` still work and now
  work regardless of which widget has focus; connect a Launchpad (or use
  the ALSA-simulator/pty tooling already in `tools/e2e/`) and confirm its
  pads shift together with the global stepper, and that its own physical
  Octave buttons shift it *relative* to wherever the global stepper
  currently sits rather than to a fixed absolute baseline.

## Open questions / left for implementation

- Exact `kOctaveWidth`/box proportions - the estimate above is rough;
  adjust once it's actually on screen.
- Whether to also expose `octave-up`/`octave-down` in the menu bar
  (`plans/menu-bar-expansion.md`'s Song section would be the natural home)
  - not required here, and easy to add once §2's `commands_.define()`
    calls exist.
- Whether a device's *offset* (as opposed to its resulting absolute
  octave) should be surfaced anywhere in the Launchpad-facing UI (e.g.
  "+1" next to its track assignment) - there's no existing per-device
  octave readout anywhere today either way, so this is a clean follow-on,
  not something this plan needs to solve.
