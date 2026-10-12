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

## Windows installer

`.github/workflows/release-windows.yml` builds an [Inno Setup](https://jrsoftware.org/isinfo.php)
installer, `synth-<version>-windows-x86_64-setup.exe`, on every `v*` tag (it
is attached to the release), on demand, and on changes to the files that make
it. `tools/windows/stage.sh` lays out the program, the MSYS2 runtime DLLs it
needs, the songs and the licence texts (and SoundFonts, below), and
`tools/windows/synth.iss` packs that. The job then installs the result with no
MSYS2 on the PATH and plays a song from it, which catches a missing DLL.

To include a General MIDI SoundFont, set the repository variables
`SYNTH_SOUNDFONT_URL` (a direct link to the `.sf2`), `SYNTH_SOUNDFONT_SHA256`
and, for its licence text, `SYNTH_SOUNDFONT_LICENSE_URL`. Without them the
installer has no SoundFont and instrument tracks use the built-in fallback
instrument. Installed copies also read SoundFonts from
`%LOCALAPPDATA%\synth\soundfonts`.

The installer is not signed, so Windows SmartScreen will warn when it is run.

