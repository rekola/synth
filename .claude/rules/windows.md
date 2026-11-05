---
paths:
  - "src/audio/Wasapi*"
  - "src/audio/AudioDevicesWasapi*"
  - "src/midi/WinMm*"
  - "src/util/Paths.*"
  - "src/util/ThreadSupport.*"
  - "src/util/FileOpen.*"
  - "src/util/StderrCapture.*"
  - "**/ShutdownSignal.*"
  - "**/Daemon.*"
  - "**/AudioBuffer.h"
  - "lib/cellui/**/win32/**"
  - "tools/windows/**"
  - ".github/workflows/*windows*"
  - "docs/building.md"
  - "CMakeLists.txt"
---

# Windows port

- Builds with MSYS2 UCRT64 (MinGW-w64 GCC — the DSP code needs GCC vector
  extensions), packages `mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,fmt,libsndfile,
  soundtouch,libunistring,python}`. User-facing instructions: `docs/building.md`.
- cellui's terminal backend is POSIX only, so on Windows `InteractiveUI` runs
  on the Win32 window (`--ui raster`, what `auto` picks; see
  `lib/cellui/CLAUDE.md`). The loop waits on handles (`WaitSet::handles()`,
  `Backend::inputHandles()`, `EventLoop::addHandles()`); stderr reaches the
  status line through `util/StderrCapture.h`, a reader thread on a pipe,
  since a pipe cannot be waited on. The pixel scopes use canvas' GDI+ backend.
- CI (`.github/workflows/ci-windows-x86_64.yml`) builds, runs ctest and
  renders songs, with `--ui headless` for the smoke test
  (`tools/smoke_headless.py`); nothing in CI runs on a desktop or against a
  device. The installer (Inno Setup) is built by `release-windows.yml` from
  `tools/windows/`.
- Keep `#ifdef _WIN32` inside the small platform helpers: `util/Paths.h`
  (`%APPDATA%\synth`, data dirs, executable dir, installed default song —
  `DeviceSettings`, `findDefaultSoundFont()` and `findDefaultSofaFile()` only
  add sub-paths), `util/ThreadSupport.h` (thread names via
  `SetThreadDescription`; `AudioThreadPriority` is MMCSS "Pro Audio" on
  Windows, nothing on Linux), `ShutdownSignal.cpp` (console control handler
  for Ctrl-C and console close), `Daemon.cpp` (`--daemon` says unsupported and
  exits), `Notifier.h` (an event), `AudioBuffer.h` (aligned allocation),
  `util/FileOpen.h` (files open by UTF-8 path).
- Device backends sit in `synth_engine` behind the same interfaces as ALSA:
  `audio/WasapiAudio` (shared-mode, event-driven; `wasapi:<endpoint id>`
  names; `AudioDevicesWasapi.cpp` lists them) and WinMM MIDI
  (`midi/WinMmMidiInput`, `WinMmMidiPorts`; `winmm::Watcher` polls the device
  list since WinMM has no hotplug notice). ALSA and PipeWire sources are
  Linux only.
