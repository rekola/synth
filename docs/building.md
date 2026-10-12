# Building

## Linux

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Produces `build/synth`. On Ubuntu the dependencies are `libnotcurses-dev
libnotcurses++-dev libfmt-dev libsndfile1-dev libasound2-dev libunistring-dev
libsoundtouch-dev libcairo2-dev`, plus CMake, pkg-config, git and a C++17
compiler. The 2D drawing library [canvas](https://github.com/rekola/canvas) is
fetched at configure time, so the first `cmake -B build` needs network access. `libmysofa-dev` (binaural
decoding) and `libpipewire-0.3-dev` (PipeWire device lists) are optional.

## Windows

The Windows build has no terminal UI: `synth --render out.wav song.xml` renders
a song and `synth --headless` plays one (see [headless.md](headless.md)), with
WASAPI audio, WinMM MIDI and a Launchpad, none of them tried on real hardware
yet. The toolchain is MSYS2 UCRT64
(MinGW-w64 GCC); in an MSYS2 UCRT64 shell:

```sh
pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,fmt,libsndfile,soundtouch,libunistring,python}
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

CI builds this on every push and uploads the binary and some rendered songs.
The plan is in [plans/windows-port.md](../plans/windows-port.md).
