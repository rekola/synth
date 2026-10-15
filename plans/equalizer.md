# Equalizer: what is left

Done: the eight-band `<equalizer>` effect, live edits while playing, the
terminal editor (curve, band table, keys, mouse), undo (a drag is one step),
tests and `tools/e2e/verify_equalizer_editor.py`.

Left:
- Pixel-graphics curve (sixel/Kitty), the way `TerminalPixelSpectrumMeter`
  draws the scope; the braille curve is used everywhere today.
- Adding an effect to a track from the UI (and listing nested effects in the
  outline, which shows only top-level tracks). Decided to solve for all effects
  together, later.
- Editing other effects' parameters in-app.
- Per-key inserts (EQ on one drum of a percussion track): out of scope.
