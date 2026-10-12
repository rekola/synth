# Cell UI

The UI library of synth: what a terminal (and later a window) interface is
built from. Design and plan: `plans/cell-ui.md`, `plans/cell-ui-terminal.md`.

    cmake -S lib/cellui -B build-cellui
    cmake --build build-cellui -j
    ctest --test-dir build-cellui --output-on-failure

It includes nothing from the application (`src/`); only libunistring is needed.
Headers are `cellui/X.h` in `namespace cellui`. So far: `Color`, `utf8`
(grapheme-aware text width), `markdown`, `ChoiceList`, the sub-cell glyph
tables, input (`Input`, `KeyChord`, `Keymap`, `CommandRegistry`), the theme
(`Theme`, `StyleProvider`), and the widget side: `UIPlane`, `Element`,
`UIMenu`, `HeatmapChart`.

## Gallery

`cellui_gallery` prints the gallery page: every text style, the theme's colour
tokens, the level meter ramp, the sub-cell glyphs, width and truncation of
wide and combining characters, a Markdown document and a choice list.

    build-cellui/cellui_gallery            # 24-bit colour escape sequences
    build-cellui/cellui_gallery --plain    # the text alone

The page is drawn by `drawGallery()` through `UIPlane` only, so any backend can
show it. `BufferPlane` is the plane that draws into a `CellBuffer` (a grid of
cells that renders itself as text or ANSI), which is what the tests and the
gallery use. The text of the page is compared with `tests/golden/gallery.txt`;
after a deliberate change, run the tests with `CELLUI_UPDATE_GOLDEN=1` to
rewrite it.
