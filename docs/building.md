# Building

## Linux

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Produces `build/synth`. On Ubuntu the dependencies are `libfmt-dev libsndfile1-dev libasound2-dev libunistring-dev
libsoundtouch-dev libcairo2-dev`, plus CMake, pkg-config, git and a C++17
compiler. The 2D drawing library [canvas](https://github.com/rekola/canvas) is
fetched at configure time, so the first `cmake -B build` needs network access. `libmysofa-dev` (binaural
decoding), `libpipewire-0.3-dev` (PipeWire device lists) and `libgtk-4-dev` (GTK 4.10 or later: the window UI,
`--ui raster`) are optional.

## Windows

The Windows build has no terminal UI; plain `synth` opens the window UI (a Win32
window drawn with Direct2D and DirectWrite, written but not yet tried on a real
desktop). `synth --render out.wav song.xml` renders
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

## macOS

The macOS build has no audio or MIDI yet, so it renders songs offline
(`synth --render out.wav song.xml`) and runs the tests; the interactive
program does not build. The toolchain is Apple clang (Xcode command line
tools) with Homebrew's libraries, not GCC:

```sh
brew install cmake ninja pkg-config fmt libsndfile sound-touch libunistring
export CPATH=/opt/homebrew/include LIBRARY_PATH=/opt/homebrew/lib
cmake -G Ninja -B build -DSYNTH_BUILD_INTERACTIVE=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

CI builds this on Apple Silicon. The scopes are drawn with canvas' Quartz 2D
backend, so Cairo is not needed.

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

The installer can add its folder to the PATH (an unchecked task in the setup;
uninstalling removes it again), so that `synth` runs from any terminal.

It also carries a SOFA file (`third_party/sofa/`, the MIT KEMAR head
responses; the licence asks only that the authors are cited) in `share\sofa`,
which binaural decoding uses. That needs libmysofa from MSYS2; the job builds
without the decoder, and leaves the file out, if the package is not there.

**Signing.** With the repository secrets `SIGN_PFX_BASE64` (a code signing
certificate as a `.pfx` file, base64 encoded) and `SIGN_PFX_PASSWORD`, the
release job signs `synth.exe` and the installer (`tools/windows/sign.ps1`,
with a timestamp), and fails if the installer's signature is not valid.
Without them the installer is unsigned and Windows SmartScreen warns when it
is run. On pull requests a throwaway certificate stands in to test the
signing. Many certificate authorities now keep the key on a hardware token or
in their cloud, which cannot be exported to a `.pfx`; such a service needs its
own signing step in place of `sign.ps1`.
