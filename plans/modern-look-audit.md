# Modern look: audit (step 1)

Two parts: the screens as the running app draws them (measured with
`tools/modern_look/capture.py`: the app on a pty under `--null-audio`, read
through pyte, at 120x40 on `songs/demo3.xml`), and every `StyleProvider`
colour from the code with where it lands in the tokens (`src/ui/Theme.h`).
Dialogs and popups beyond the M-x minibuffer (the choice list, info dialogs,
the Devices menu) are not captured yet.

## Screens, measured

Live View and Arrangement view, 4800 cells each.

**Colours.** Only the 256-colour palette shows up (`TERM=xterm-256color`; the
capture says nothing about true colour). Backgrounds in use, with the area:

| Background | Where | Verdict |
|---|---|---|
| `#080808` | most of the screen | `surface.base` |
| `#5f005f` | the whole menu bar: saturated purple | `surface.panel`, `text.secondary` |
| `#767676` | the info line, dark text on mid grey | `surface.panel`, `text.primary` |
| `#000000` | the bottom message line | `surface.base` |
| `#3a3a3a` / `#303030` | outline heading, bar rows | `surface.raised` |
| `#5f5f00` | the master heading: olive, a full-width block | `surface.raised` plus a chip |
| `#af5f5f` `#5faf87` `#875faf` `#afaf5f` `#5fafaf` | each track's heading, a full-cell block of strong colour | a 1-cell track chip on `surface.raised`; colour from the formula |
| `#5f8787` | the pattern-editor cursor row: teal | `accent.dim` row, `accent` on the cell |
| `#5f5f5f` | the spectrum/clip tint | `surface.panel` |
| `#d7d7ff` | a selected clip slot | `accent` |

So: five strong full-cell colour blocks per screen (menu, master, track
headings, cursor, info), which is the "blocks of strong colour" the plan calls
dated.

**Lines.** 152 box-drawing cells in Live View and 137 in Arrangement view,
all `│` between every track column and between the outline and the grid; none
are containers. The Sends/Direction block also uses ASCII `-` rows as
separators (`------`), and `▌` as a heading mark. Proposed: none of them;
columns are separated by a one-cell gutter on the base surface.

**Glyphs.** `⏹ ▸ ◂ ▌ ✎ ♭` and the middle dot, plus `🖼`, an emoji, in the
Arrangement view's scope row, which is the one glyph from another font family
and the one that is double width. It has no counterpart in the icon list and
should become one.

**Type.** Bold is used for headings and the focused item only; no italics seen.
Nothing to change beyond keeping it so.

**Gutters.** Columns are separated by lines, so there is no gutter; the
clip grid's header row, its slots and the Sends block each get a one-cell
gap in the mock.

**Message line.** The bottom line shows a capture warning ("no capture device
available") in plain text on black; a warning should use a state colour once
it has one.

## Colours from the code

## Covered by tokens (the dark theme matches the old values exactly)

| Field | Token |
|---|---|
| `highlight_fg_color` | text_on_accent |
| `highlight_bg_color` | accent (focus) |
| `highlight_unfocused_bg_color` | accent_dim |
| `window_bg_color` | surface_base |
| `window_accent_bg_color` | surface_panel |
| `heading_bg_color` | surface_raised |
| `window_border_color` | surface_overlay (also used as the Monitor "Off" text, which is really text_disabled) |
| `window_fg_color` | text_secondary |
| `window_accent_fg_color` | text_primary |
| `window_beat_accent_*`, `window_bar_accent_*` | grid_beat_*, grid_bar_* |
| `clip_playing_color` / `clip_paused_color` | state_playing / state_playing_dim |
| `clip_recording_color` / `clip_armed_color` | state_recording / state_recording_dim |

## Still named colours (decide in step 4)

- Cursor tints: `cursor_tint_color`, `cursor_row_tint_color` (derive from accent).
- Roles with no token yet: `command_column_color`, `master_track_color`
  (a dimmed command colour), `reader_text_color`, `clip_text_color`,
  `info_line_*`, `button_*`, `row_pressed_bg_color`.
- Saturated primaries on black to replace: `mute_color`, `solo_color`,
  `monitor_color`, `clip_override_color`, `locator_*`, `meter_*`,
  `spectrum_*` (the meter ramp stays a ramp token).
- `text_disabled` is `#323232` only to keep today's look; it fails the 3:1
  contrast rule and is the first thing to fix in step 4.
- No separate queued state exists: queued and playing share a colour and
  differ by glyph shape.

## Hard-coded outside `StyleProvider`

`PatternEditor.cpp` (about lines 3570 and 3621) holds three literals for
the note-column types: `#d65cd6`, `#bfa426`, `#42c1ea`. They need tokens.

## Usage counts

`meter_{low,mid,lime,warn,hot}_color` and `spectrum_{low,high}_color` are
read only inside `StyleProvider` (through `meterColor()`/`spectrumColor()`);
`cursor_row_tint_color` only through `cursorRowTint()`.
