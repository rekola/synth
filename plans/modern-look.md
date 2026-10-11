# Modern look

Make the UI look current rather than dated, in the terminal now (steps 1-5,
part of `plans/cell-ui-terminal.md`) and in the window later (step 6,
`plans/cell-ui-window.md`). Most of this is about decisions,
not rendering features, so it starts in the terminal. Every colour is
already a `StyleProvider` constant, which is the lever.

## What reads as dated

- Lines everywhere: boxes around every widget, double borders, separators
  between every column.
- Saturated primaries (pure red, green, blue, yellow) on black, and full-cell
  blocks of strong colour.
- Bevels, glossy gradients, heavy 3D.
- Inconsistent icons (a mix of ASCII art, emoji and symbols from different
  fonts), all caps, many fonts or weights at once.
- Everything the same brightness, so nothing leads the eye.
- Instant jumps on every state change.

## Principles

1. **Group by surface, not by line.** A few background tones (elevation
   levels: base, panel, raised, overlay) separate areas. A line is used only
   where tone can't do it. A floating layer gets a shadow in the window and a
   one-step lighter tone in the terminal.
2. **Neutrals plus one accent.** Slightly cool greys for surfaces and text in
   three emphasis levels (primary, secondary, disabled), one accent for focus
   and the cursor (the cursor is already cyan-leaning), and semantic colours
   reserved for state: playing green and recording red, with their dim forms.
   Queued is not a colour: a queued clip uses the playing (or recording)
   colour and differs by pulsing on the pads and by its glyph on screen.
3. **Track colours and clip states are shared with the Launchpad, so they do
   not change with the theme.** The Launchpad is always dark and has no icons,
   so a clip's colour and state must be identical on the pads and the screen:
   one value per token in every theme, in the lightness band that is readable
   on both the darkest and the lightest surface. Track colours come from one
   formula in a perceptual colour space (OKLCH): fixed lightness and chroma,
   hue spread evenly over the arcs the green and red state colours leave free,
   so a track never reads as a playing or a recording clip. See
   `plans/modern-look-tokens.md`.
4. **Type hierarchy with few levels.** Mono for data (notes, hex values),
   the UI font for labels in the window backend. Two sizes, two weights.
   In the terminal: bold only for headings and the focused item, italic
   for placeholders.
5. **One icon set, our own.** One list of icon meanings, each drawn as our
   own SVG in one consistent style (stroke width, corner radius, grid),
   drawn as pixels in the window and in terminals with Kitty graphics or
   Sixel, with a Unicode fallback for other terminals chosen to render in
   common monospace fonts (▸ ▹ ● ○ ◆ ✎ ⏹ are the base), checked against
   the width probe. An icon without a fitting character uses one shared
   generic icon glyph; icons that matter for using the program get their
   own, since the glyph is also what screen readers and braille displays
   get. The icons are part of the look, so they are designed
   with the tokens, not picked from a stock family.
6. **Motion that explains.** 120-250 ms eased transitions where a state
   changes in place: queued to playing, a panel opening, the cursor moving
   to an undo place. The terminal does this as colour fades, as it already
   does for loop passes. Reduced motion turns it off.
7. **Density as a choice.** A compact and a comfortable density (row
   height and padding in the window; gutters in the terminal), compact by
   default since this is a tracker.
8. **Contrast is measured, not eyeballed.** Text against its surface meets
   WCAG AA (4.5:1 for body text, 3:1 for large text and UI glyphs). A
   high-contrast theme exists from the start, because it is cheap once the
   colours are tokens.

## Theme tokens

`StyleProvider` turns from a list of named colours into tokens that the
widgets use by role:

- surface.base / .panel / .raised / .overlay
- text.primary / .secondary / .disabled / .on-accent
- accent, accent.dim, focus
- state.playing / .recording (+ dim forms), the same in every theme; queued
  uses these (pulse on the pads, glyph on screen)
- track colours (8 from the formula) and text.on-track, the same in every theme
- meter ramp (already `meterColor()`)
- grid lines: bar, beat (as tones, not lines, where possible)

Themes are data:

- dark (the default), light and high contrast;
- in the window backend, follows the OS preference unless set.

## Terminal specifics

- True colour where available; elsewhere map each token to the nearest
  palette entry by perceptual distance, not by RGB distance.
- Use box-drawing only for real containers (dialogs, popups). Rounded corners
  (╭ ╮ ╰ ╯) for floating ones.
- One-cell gutters between widgets, filled with the base surface rather
  than drawn lines.
- Half-block (▀ ▄) edges where a surface change needs a softer boundary than
  a full cell.

## Steps

1. **Audit:** screenshot every view and dialog in the terminal, list each
   colour, glyph and line used, and map each to a token or mark it for
   removal. Views done (`plans/modern-look-audit.md`, captured with
   `tools/modern_look/capture.py`); dialogs still to do.
2. **Mock:** an HTML mock of Live View and Arrangement view in the proposed
   tokens, dark and light, to agree on the direction before code. This is
   also the reference for the window backend. Done:
   `plans/modern-look-mock.html`, with the token values and contrast checks in
   `plans/modern-look-tokens.md`; the direction is still to be agreed.
3. **Tokens:** restructure `StyleProvider` into tokens, with the dark theme
   matching today's look exactly. This step is pure refactoring.
4. **Apply in the terminal:**
   - the new surfaces and neutrals;
   - track colours from the formula;
   - fewer lines;
   - the icon list;
   - fades for state changes.
5. **Light and high-contrast themes,** with a theme command and a setting.
6. **Window backend:** the same tokens plus elevation shadows, radius,
   padding, the UI font and smooth motion, in `plans/cell-ui-window.md`'s
   styles step.
