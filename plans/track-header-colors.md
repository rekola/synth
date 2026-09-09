# Per-track palette colors + selection highlighting in the pattern heading

## Context

`PatternEditor::renderHeading()` currently draws every non-effect leaf
track's title bar (level 0) in one fixed orange (`0xf0,0x80,0x10`), and
every effect's own column/ancestor-row box in a grey shaded by nesting
depth (`segment_color()`'s two branches). Both branches draw black
(`0x00,0x00,0x00`) text. `InstrumentTrack` already has a persisted
`color_`/`getColor()`/`setColor()` string field (XML `color="..."`
attribute) that nothing in the UI ever reads - dead since it was added.
There is no notion of "the cursor's current track" reflected in the
heading at all today; `current_cursor.track` only drives the pattern
grid's own row/column highlighting.

Not just a `PatternEditor` concern: `plans/per-track-patterns-scenes-matrix.md`
describes a (future, unimplemented) Renoise-style Pattern Matrix that
will also want to show which track is which at a glance - the same
tracks the pattern editor heading already colors. Whatever assigns a
track's color has to live somewhere both UIs can call, and has to be
deterministic from the same inputs, or the same track could show up a
different color in each - see the new `src/ui/tui/TrackColor.h` in Design
below.

Decisions from clarification (see conversation):
- Auto-assigned palette colors only - no manual per-track override for
  now. The dead `color_` field is deleted rather than repurposed.
  Keyed off each track's *ordinal position* among its siblings in the
  heading (see below), not `getInternalId()` - that counter is
  process-global and shared by every `SongObject`, not just tracks
  (scenes/patterns/instruments all consume it too), so two tracks in
  the same song can land arbitrarily close or far apart in it with no
  relationship to their actual position in the track list; ordinal
  position is also what a reader visually expects palette cycling to
  follow.
- Only `TrackType::INSTRUMENT_CONTROL`/`PERCUSSION_CONTROL`/`SAMPLE`/
  `DRUM_MACHINE` get an assigned color (`Arpeggiator` is an
  `INSTRUMENT_CONTROL` subclass, already covered - all four are the
  content-bearing, non-effect track types). Everything else (currently
  just `TrackType::EFFECT`, plus `GROUP` should it ever gain a heading
  box of its own) falls into the same "no assigned color" bucket and
  gets the existing depth-shaded grey instead.
- Mute/Solo's OFF color becomes a darkened version of the track's own
  resolved base color - palette color if it has one, grey otherwise -
  replacing the current fixed orange; the ON
  (muted/soloed) look - glyph goes black, blending into the header -
  stays exactly as it is today. This is orthogonal to selection.
- A selected track's header is a *brightened* version of its own
  resolved background (blended toward white), not a fixed shared
  highlight color - keeps each track's identity visible while still
  reading as "selected", and works the same way for both an instrument
  title bar and a grey effect box.

## Design

### `UIColor::fromHSL()` (new, `src/ui/tui/UIColor.h`)

`UIColor` currently only builds from RGB (an int triple or a hex
string). Add a small static HSL->RGB constructor alongside the existing
`blend()` member - the standard piecewise formula (hue in degrees
`[0,360)`, saturation/lightness in `[0,1]`), self-contained, no new
dependency:

```cpp
static UIColor fromHSL(float h, float s, float l);
```

### `src/ui/tui/TrackColor.h` (new)

Not `PatternEditor.cpp`-local: the pattern editor heading is not the
only consumer - `plans/per-track-patterns-scenes-matrix.md`'s (future,
not yet implemented) Pattern Matrix will need to color-code the same
tracks too, and the same track must resolve to the same color in both
places, or the whole point of a per-track color (recognizing a track at
a glance) breaks the moment there are two UIs to recognize it in. So
the color-assignment *primitives* live in their own small header,
dependent only on `Song`/`Track`/`TrackType`/`UIColor` - no dependency
on `PatternEditor` or anything heading-specific - and either caller
builds whatever presentation it needs (grey fallback, selection
highlighting, ...) on top:

```cpp
// Whether `type` is one of the content-bearing track types that get an
// assigned color at all (instrument/percussion/sample/drum-machine) -
// shared so "does this track have a color" can never disagree between
// the pattern editor heading and the Pattern Matrix.
bool isColorEligibleTrackType(TrackType type);

// internal id -> position among color-eligible tracks only, in the
// order they appear in `track_ids`. Both current (renderHeading) and
// future (Pattern Matrix) callers pass the *same* `song.getRootTrackIds()`
// list here, so they necessarily agree on every eligible track's
// ordinal - and hence its color - without needing to share any other
// state. O(track_ids.size()); call once per render pass, not per track.
std::unordered_map<int, int> computeTrackColorOrdinals(const std::vector<int> & track_ids, const Song & song);

// Golden-angle-generated hue, fixed saturation/lightness - see below.
UIColor trackHeaderColor(int ordinal);
```

No hardcoded RGB swatches in `trackHeaderColor()` - the hue is
*generated* from `ordinal`, saturation and lightness are fixed
constants tuned once for "darkened and desaturated enough for white
text":

```cpp
constexpr float kTrackColorSaturation = 0.35f;
constexpr float kTrackColorLightness = 0.32f;
// Golden-angle hue step (360 / phi^2 degrees) - the standard technique
// for generating a sequence of hues that stay visually well-spread from
// each other no matter how many tracks exist, rather than a fixed-size
// palette that starts repeating after N tracks.
constexpr float kGoldenAngle = 137.50776f;

UIColor trackHeaderColor(int ordinal) {
  float hue = std::fmod(ordinal * kGoldenAngle, 360.0f);
  return UIColor::fromHSL(hue, kTrackColorSaturation, kTrackColorLightness);
}
```

Saturation/lightness values above are a starting point - tune by eye
once running; the mechanism (fixed S/L, generated H) is the point, not
the exact numbers.

`computeTrackColorOrdinals()`'s ordinal **must** count only
color-eligible tracks, not every entry in `track_ids`
(`song.getRootTrackIds()`/`SongStructure`'s full depth-first-ordered
list, which also includes every effect track, and would include
`GROUP` too if that ever got its own ordinal). Counting every track
would let a degenerate song's effect tracks - which never get a color
themselves - "use up" ordinals between two instrument tracks for
nothing, so which specific hues the *visible* colored tracks end up
with would depend on how many uncolored tracks happen to sit between
them, rather than purely on how many colored tracks came before them.
Still fully deterministic and still unaffected by scrolling/cursor/
anything runtime - just counted over the color-eligible subsequence
only, so it only changes when a color-eligible track is actually added,
removed, or reordered relative to other color-eligible tracks
(adding/removing/reordering an *effect* never reassigns any instrument/
percussion/sample/drum-machine track's color). Implementation is a
single pass over `track_ids`, assigning the next sequential integer to
each id that passes `isColorEligibleTrackType()`.

### `renderHeading()` changes (`src/ui/tui/PatternEditor.cpp`)

**Palette instead of fixed orange, only for color-eligible types.**
`renderHeading()` calls the shared `computeTrackColorOrdinals(track_ids, song)`
once near its top (not inside the per-level loop - purely structural,
independent of `level`), then builds its own local
`track_base_color(Track * t) -> UIColor` on top of it - the grey
fallback and depth-shading are heading-specific presentation, not
something `TrackColor.h` needs to know about:

```cpp
auto color_ordinal = computeTrackColorOrdinals(track_ids, song);

auto track_base_color = [&](Track * t) -> UIColor {
  if (!t) return styles.window_bg_color;
  if (level == 0 && isColorEligibleTrackType(t->getType())) {
    return trackHeaderColor(color_ordinal.at(t->getInternalId()));
  }
  auto step = std::min(t->getDepth(), 6);
  auto v = 0x30 + step * 0x10;
  return UIColor(v, v, std::min(v + 0x10, 255));
};
```

`getInternalId()` is only ever used here as a map *key* (the natural,
already-everywhere identifier for "which Track object"), never as the
color input itself - the color comes from `color_ordinal`'s looked-up
*value*, which `computeTrackColorOrdinals()` assigns purely by position
among color-eligible tracks, exactly as required.

`isColorEligibleTrackType()` is an allow-list
(`INSTRUMENT_CONTROL`/`PERCUSSION_CONTROL`/`SAMPLE`/`DRUM_MACHINE`)
rather than a deny-list (`!= EFFECT`) - for these four types the two
are equivalent today (nothing else reaches this branch), but the
allow-list states the actual intent ("content-bearing track types get
a color") so a future non-color-worthy, non-effect track type falls
through to grey by default instead of silently picking up a color it
was never meant to have. `computeTrackColorOrdinals()` uses the exact
same predicate internally, so the two can never drift apart - a track
`track_base_color()` decides is color-eligible is always exactly one
`computeTrackColorOrdinals()` already assigned a slot to. `segment_color(t)`
becomes `track_base_color(t)` plus the selection brighten (below); no
signature changes needed at any of its call sites (`segment_color(track)`
at level 0, `segment_color(left)` inside `draw_divider`,
`next_segment_color`'s lookup at `tracks[idx + 1]`) - the ordinal is
looked up on demand from `t->getInternalId()`, no threading through the
per-level merge logic needed.

**Selection highlight.** Compute once, near the top of `renderHeading()`:

```cpp
auto selected_id = current_cursor.track < static_cast<int>(track_ids.size())
  ? track_ids[static_cast<size_t>(current_cursor.track)] : -1;
```

(same `track_ids[current_cursor.track]` identity already used by
`toggle-mute`/`toggle-track-collapse`/etc.). `segment_color(t)` calls
`track_base_color(t)` and, when `t->getInternalId() == selected_id`,
blends the result toward white
(e.g. `.blend(0.35f, UIColor(255,255,255))` - exact factor to be tuned
by eye) before returning it. Because every heading fill *and* every
divider (`draw_divider`/`draw_edge`) already goes through
`segment_color`, this single change highlights the whole box
consistently - both a level-0 title bar and *every* row of a
multi-level effect ancestor box (not just the row its name happens to
draw on) - with no separate special-casing needed anywhere else. This is
what makes "including effect track" (from the request) fall out for
free, for both color-eligible and grey (effect) headers alike.

**Text color: black -> white.** Both places `renderHeading()` sets
`setFgColor(0x00, 0x00, 0x00)` to draw a track's own name (the level-0
title bar and the effect ancestor-row box) switch to white
(`0xff, 0xff, 0xff`). Required by the darker palette (black-on-dark
would be close to unreadable); the existing near-white instrument
sub-label line (`0xf0,0xf0,0xf0`) is already fine as-is.

**Mute/Solo OFF color.** Goes through `track_base_color(track)` (not
`trackHeaderColor()` directly), so it stays correct automatically for
any non-effect track that isn't color-eligible - none exist today
(`DRUM_MACHINE` is in the allow-list above, same as the other three),
but this keeps the OFF color from ever calling `trackHeaderColor()`
with a meaningless ordinal for a hypothetical future one. Replace the
fixed `setFgColor(0xe0, 0x70, 0x08)` (OFF branch only, for both M and
S) with `track_base_color(track).blend(0.4f, UIColor(0,0,0))` (blend
factor to be tuned by eye). The ON branch (`setFgColor(0x00, 0x00, 0x00)`,
i.e. the glyph blends into the header when actually muted/soloed) is
untouched.

**Collapse toggle contrast check.** The "+"/"-" toggle glyph
(`styles.window_border_color`, `#323232`, added in the previous
session) was tuned for high contrast against the old bright orange; the
new palette is much darker, so that dark-grey glyph risks nearly
disappearing on an instrument-family header. Move it to
`styles.window_fg_color` (`#9e9e9e`, the UI's standard mid-grey text
tone) instead - visible against both the new palette and the unchanged
grey effect boxes, while still reading as more muted than the white
name text. Needs a visual look once running; this is a starting value,
not a hard requirement.

### `InstrumentTrack` cleanup (`src/model/InstrumentTrack.h`/`.cpp`)

Delete `color_`, `getColor()`, `setColor()`, the `setColor(input.getText("color"))`
line in `loadParameters()`, and the `output.set("color", getColor())`
line in `storeParameters()`. Confirmed unused anywhere else in `src/` or
`tests/`. Several `songs/*.xml` fixtures already carry a stale
`color=""` attribute (always written, even empty, by the old
`storeParameters()`) - harmless to leave as-is; XML loading already
ignores attributes nothing reads, and the attribute simply stops being
written the next time each file is saved.

## Files touched

- `src/ui/tui/UIColor.h` - new `fromHSL()` static constructor.
- `src/ui/tui/TrackColor.h` - new, shared and reusable beyond
  `PatternEditor` (see Context): `isColorEligibleTrackType()`,
  `computeTrackColorOrdinals()`, `trackHeaderColor()` (golden-angle hue
  + fixed saturation/lightness).
- `src/ui/tui/PatternEditor.cpp` - `renderHeading()`'s `segment_color()` and
  the level-0/ancestor-row name/M-S/toggle drawing.
- `src/model/InstrumentTrack.h`/`.cpp` - delete the dead `color_` field.

No model/state/persistence additions - colors are derived purely from
each track's `getType()` and its position among color-eligible tracks,
recomputed fresh every `renderHeading()` call, nothing new to save.

## Verification

- `cmake --build build -j` clean, `ctest --test-dir build` green (no
  behavioral/audio changes - this only touches heading rendering and
  removes a dead field).
- Manual visual check: `./build/synth songs/demo3.xml` (or another song
  with a mix of instrument/percussion/sample/drum-machine/effect
  tracks) - confirm distinct instrument/percussion/sample/drum-machine
  colors, grey effect boxes, white legible text, a visibly brightened
  header for whichever track the cursor is on (moving it with
  Left/Right), and legible M/S + collapse-toggle
  glyphs in both the OFF and idle-unselected states.
