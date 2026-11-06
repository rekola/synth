---
paths:
  - ".github/workflows/*macos*"
  - "src/util/Notifier.*"
  - "src/util/Paths.*"
  - "**/CanvasRasterizer*"
  - "docs/building.md"
  - "CMakeLists.txt"
---

# macOS port

Builds with Apple clang (Xcode command line tools) and Homebrew: `brew install
cmake ninja pkg-config fmt libsndfile sound-touch libunistring`. Not GCC:
canvas' Quartz 2D backend includes Apple's framework headers (blocks syntax),
and Homebrew's `fmt`/`sound-touch` are built against libc++, which GCC's
libstdc++ cannot link. Clang does not search `/opt/homebrew`, so set
`CPATH=/opt/homebrew/include` and `LIBRARY_PATH=/opt/homebrew/lib`. There is
no audio or MIDI backend yet, so only `-DSYNTH_BUILD_INTERACTIVE=OFF` (the
render-only `synth`, the engine and the tests) builds;
`.github/workflows/ci-macos-arm64.yml` does that, runs ctest and renders
songs. Clang optimizes on `-ffinite-math-only` harder than GCC, so
`CMakeLists.txt` keeps `-fno-finite-math-only` with `-ffast-math` there. The
pixel scopes draw through canvas' Quartz 2D backend (no Cairo or fontconfig);
Core Text falls back to another font for a missing glyph by itself, so
`CanvasRasterizer` has no fontconfig there. `Notifier` is a pipe (no eventfd)
and `executableDir()` uses `_NSGetExecutablePath`.
