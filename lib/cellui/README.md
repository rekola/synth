# Cell UI

The UI library of synth: what a terminal (and later a window) interface is
built from. Design and plan: `plans/cell-ui.md`, `plans/cell-ui-terminal.md`.

    cmake -S lib/cellui -B build-cellui
    cmake --build build-cellui -j
    ctest --test-dir build-cellui --output-on-failure

It includes nothing from the application (`src/`); only libunistring is needed.
Headers are `cellui/X.h` in `namespace cellui`. So far: `Color`, `utf8`
(grapheme-aware text width), `markdown`, `ChoiceList` and the sub-cell glyph
tables.
