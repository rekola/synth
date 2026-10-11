# Modern look: colour audit (step 1, from the code)

Every `StyleProvider` colour, where it lands in the tokens (`src/ui/Theme.h`),
or what still needs a decision. Screenshots of every view and dialog, and the
glyph and line audit, are still to do (they need a terminal session); this
covers colours.

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
