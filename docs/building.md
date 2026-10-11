# Building

## Linux

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Produces `build/synth`. On Ubuntu the dependencies are `libnotcurses-dev
libnotcurses++-dev libfmt-dev libsndfile1-dev libasound2-dev libunistring-dev
libsoundtouch-dev`, plus CMake and a C++17 compiler. `libmysofa-dev` (binaural
decoding) and `libpipewire-0.3-dev` (PipeWire device lists) are optional.

## Windows

Windows support is just starting: only the engine, the tests and a render-only
`synth` (`synth --render out.wav song.xml`) build there. There is no terminal
UI, realtime audio, MIDI or Launchpad yet. The toolchain is MSYS2 UCRT64
(MinGW-w64 GCC); in an MSYS2 UCRT64 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,fmt,libsndfile,soundtouch,libunistring,python}
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

CI builds this on every push and uploads the binary and some rendered songs.
The plan is in [plans/windows-port.md](../plans/windows-port.md).
