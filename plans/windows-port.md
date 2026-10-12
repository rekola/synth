# Windows port

Goal: a native Windows build, reached in steps that are each useful on their
own, without deciding the graphical UI yet. Step 0 gets a CI-built
command-line binary that renders songs. Steps 1-5 make the platform layer
(audio, MIDI, Launchpad, OS utilities) backend-neutral and add Windows
backends, ending with `--headless` (playback + Launchpad performance) working
on Windows; step 6 packages it. This runs alongside phase 1 of the Cell UI
(`plans/cell-ui-terminal.md`), which replaces notcurses: once both are far
enough, the terminal UI runs in a Windows console too. The graphical UI is
phase 2 (`plans/cell-ui-window.md`) and not part of this plan.

## Decisions

- **Toolchain: MSYS2 UCRT64 (MinGW-w64 GCC), not MSVC.** The DSP code uses
  GCC vector extensions (`dsp/Vec8.h`, `ChannelBank.h`, `HalfbandFilter.h`,
  `__attribute__((vector_size))`/`__builtin_*`), which MSVC doesn't support,
  and the whole `-Werror=` flag set in `CMakeLists.txt` is GCC syntax. MSYS2
  also packages every dependency (fmt, libsndfile, soundtouch, libunistring,
  probably libmysofa); libunistring under MSVC/vcpkg is the awkward one.
  clang-cl/MSVC can be revisited later; nothing here should make it harder.
- **CI**: GitHub's hosted Windows runner (pinned image, like the Linux jobs),
  `msys2/setup-msys2` with `msystem: UCRT64`, `-DSYNTH_MARCH=x86-64-v2`.
- **No `#ifdef _WIN32` scattered through engine code** as the end state.
  Step 0 may add a few where unavoidable; each is replaced by a platform
  abstraction in steps 1-3.

## What is Linux-only today

Engine library (`synth_engine`, which tests and `--render` link):
- `playback/EventQueue.h` - `eventfd` wakeup (`sys/eventfd.h`, `unistd.h`).
- `audio/AudioAPI.h` - includes `poll.h`, exposes `pollfd` descriptor lists
  that `playback/Player.cpp`'s audio loop polls together with the queue's
  eventfd.
- `playback/VisualizationThread.cpp` - `poll.h`.
- `util/ShutdownSignal.cpp` - `sigaction`, `SIGHUP`.
- `Controller.cpp` `findDefaultSoundFont()`, `audio/DeviceSettings.cpp` -
  `HOME`/`XDG_CONFIG_HOME` and `/usr/share/...` paths.

Executable only:
- `audio/AlsaAudio.*` (PCM + ALSA sequencer MIDI input),
  `audio/AudioDevices.cpp` (ALSA/PipeWire listing),
  `launchpad/LaunchpadIO.*` (ALSA sequencer, hotplug, SysEx).
- `main.cpp` (`termios.h`, `signal.h`, `unistd.h`, notcurses include),
  `util/Daemon.cpp`, `ui/UI.cpp` (`pthread_setname_np`),
  `ui/headless/HeadlessUI.cpp`, all of `ui/tui/`.

Tests: `tests/CMakeLists.txt` compiles `ui/tui/EscapeCoalescer.cpp` (needs
notcurses headers) into `synth_tests`.

## Step 0 - Windows CI: build the engine, run the tests, render songs

Prompt for a fresh session:

```
Add a Windows CI job to this repository (see CLAUDE.md first). The aim is a
headstart on a Windows port, not a usable app: on Windows we only need
synth_engine, the test suite and a command-line binary that can --render a
song to WAV. Everything interactive (terminal UI, ALSA audio/MIDI,
Launchpad, PipeWire, --headless, --daemon) is out of scope and must simply
not be built there. Read plans/windows-port.md for the context, the
toolchain decision and the list of Linux-only code.

1. Workflow: .github/workflows/ci-windows-x86_64.yml, modelled on the Linux
   x86-64 job: pinned Windows runner image, msys2/setup-msys2 with
   msystem UCRT64, packages for gcc, cmake, ninja, fmt, libsndfile,
   soundtouch, libunistring (and libmysofa if MSYS2 has it - binaural is
   optional either way). Configure with -DSYNTH_MARCH=x86-64-v2, build,
   run ctest, then render a few songs from songs/ (demo3.xml at least) with
   the built binary and fail if the WAV is missing or empty. Upload the
   rendered WAVs and the binary as workflow artifacts so they can be
   listened to. Do not trigger on anything the Linux jobs don't.

2. CMake: on WIN32 (or a SYNTH_BUILD_INTERACTIVE option defaulting OFF on
   Windows, ON elsewhere - pick one and say why) build synth_engine, the
   tests, and a render-only executable; skip the notcurses/ALSA/PipeWire
   finds and sources. The Linux build and its outputs must not change -
   same targets, same flags, same binary name. GCC flags stay; if a flag
   is rejected by MinGW GCC, find out why rather than dropping it globally.
   tests/: leave EscapeCoalescer/notcurses-dependent tests out of the
   Windows build only.

3. main: the render-only binary must accept the same --render invocation
   as Linux (plus --help/--licenses and the channel/mixer options --render
   honours). Prefer splitting main.cpp so the argument handling and
   renderSongToWav() are shared and the interactive half is a separate
   translation unit, over #ifdefs through main.cpp.

4. Engine portability: make synth_engine compile on MinGW with the smallest
   changes that keep Linux behaviour identical - EventQueue's eventfd,
   AudioAPI.h/VisualizationThread.cpp's poll.h, ShutdownSignal's
   sigaction/SIGHUP, HOME-based paths (use USERPROFILE/APPDATA on
   Windows). Where a real abstraction is planned in plans/windows-port.md
   (steps 1-3), a narrow #ifdef _WIN32 is fine now; keep each one obvious
   and few. If Player/VisualizationThread/Controller pull in more than
   that, look at whether --render actually needs them before porting them.
   File paths: songs reference sample sidecars and SoundFonts by path -
   check std::filesystem use with backslashes and non-ASCII paths
   (Windows wide-char paths) for the render path at least.

5. Tests must pass on Windows as on Linux. The golden render hash
   (render_golden_hash_catches_randomization_regressions) is gated on
   SYNTH_MARCH=x86-64-v2; if it differs on Windows, find out why (libm
   differences are the likely cause) before deciding whether to gate it by
   platform too - document the reason in the test if so. Do not skip or
   disable any other test; a failure is a real portability bug to fix.

6. Docs: a short Windows section in CLAUDE.md's Build part and the README
   (MSYS2 UCRT64 packages, what works there today: render + tests only).

Validate locally that the Linux build and ctest are unchanged before
pushing; iterate on the Windows job via CI until it is green. Commit and
push to this session's branch.
```

Expected outcome: a green Windows job, downloadable WAVs rendered by a
Windows binary, and a short list of the `#ifdef`s step 0 had to add (each
to be removed by a later step).

## Step 1 - Portable wakeup and wait (done, except MIDI)

Done: `util/Notifier`, `AudioAPI::wait()` (playback/capture/woken), `NullAudio`
and `--null-audio`; `Player::play()` is portable. The MIDI descriptors the UI
loops poll move with step 2.

The audio thread is a blocking loop: `Player.cpp` polls the PCM descriptors,
the MIDI descriptors and the playback `EventQueue`'s eventfd. Windows has no
fds for any of these (WASAPI and MIDI give event `HANDLE`s).

- `util/Notifier` (or similar): the wakeup `EventQueue` signals. Linux keeps
  eventfd; Windows uses an auto-reset Event. `EventQueue` stops including
  `unistd.h`/`sys/eventfd.h`.
- Replace `AudioAPI`'s `pollfd` lists with a wait the backend owns, e.g.
  `AudioAPI::wait(Notifier & wake, int timeout_ms) -> WaitResult`
  (playback ready / capture ready / MIDI ready / woken / timeout). ALSA
  implements it with `poll()` over its descriptors plus the notifier's fd
  (exactly today's behaviour); WASAPI with `WaitForMultipleObjects`.
  `Player` no longer sees descriptors; `devices_changed_` becomes "the
  backend rebuilds its own wait set".
- `VisualizationThread` uses the same notifier wait.
- Add a `NullAudio` backend (timer-paced, no device): lets `--headless`
  and an integration test run the real `Player` loop on CI on both
  platforms, which have no sound hardware.

## Step 2 - Split MIDI and Launchpad I/O from audio (done, except Windows names)

Done: `midi/MidiInput.h` (`AlsaMidiInput`), `midi/MidiPorts.h`
(`MidiPortBackend`, `AlsaMidiPorts`), `LaunchpadIO` platform-neutral with tests
against a fake backend, and `util/WaitSet` (Notifiers, plus fds on POSIX) for the
UI loops; `AudioAPI` no longer knows MIDI or descriptors. Left: the Windows
port names in `modelFromDeviceName()`, and the WinMM backends (step 4).

- `AudioAPI::recordMIDI()`/`setMidiInput()` move to a `MidiInput`
  interface (list sources, connect by name, poll events, reconnect on
  reappear - today's `AlsaAudio::recordMIDI()` logic). The UI thread reads
  it as now.
- `LaunchpadIO`'s ALSA sequencer code goes behind a `MidiPortBackend`
  (enumerate ports with names, open in/out pair, send SysEx/short
  messages, receive, hotplug notifications or polling). `LaunchpadIO`
  keeps the protocol side (`LaunchpadProtocol`, `acceptsClient()`, LED
  clearing) and becomes platform-neutral.
- `LaunchpadProtocol::modelFromDeviceName()` must recognise Windows port
  names (e.g. "LPX MIDI", "MIDIIN2 (LPX MIDI)"); confirm exact names on
  hardware before adding them.
- The e2e simulators stay ALSA-only; that is fine - they test the Linux
  backend and the shared logic above it.

## Step 3 - OS utilities (done)

Done: `util/Paths`, `util/ThreadSupport` (names, `AudioThreadPriority`), a
console control handler in `ShutdownSignal`, and `--daemon` refusing on
Windows. The Windows install layout (where `share` lives next to the
executable) is settled by step 6.

- Thread naming (`UI.cpp`): `SetThreadDescription` on Windows.
- Shutdown (`ShutdownSignal.cpp`): `SetConsoleCtrlHandler` for Ctrl-C /
  console close; SIGHUP has no equivalent.
- Config and data dirs: one helper for "config dir" (`%APPDATA%\synth`),
  "user data dir" and system search dirs, used by `DeviceSettings`,
  `findDefaultSoundFont()`, `findDefaultSofaFile()` and the default song
  lookup (`InstallPaths.h.in` - relative to the executable on Windows).
- `--daemon`: not supported on Windows; say so and exit (a service wrapper
  is a separate question).
- Real-time priority for the audio thread: MMCSS (`AvSetMmThreadCharacteristics`
  "Pro Audio") on Windows, whatever Linux does today unchanged.

## Step 4 - Windows backends (written, not yet run on hardware)

- **WASAPI audio** (`audio/WasapiAudio.*`): shared mode, event-driven,
  float32, the song's sample rate (resampling by the engine if the mix
  format differs is out of scope - fail with a clear message first, as a
  mismatched ALSA device does now). Capture likewise. Delay queries for
  latency compensation from `GetStreamLatency` + current padding.
  Exclusive mode later, as an option.
- **Device listing** (`AudioDevices`): `IMMDeviceEnumerator`, labels from
  the friendly name, disambiguated like `DeviceLabels.h` does. Device
  names in `DeviceSettings`: a `wasapi:<endpoint id>` scheme next to
  `pw:`; `""`/`default` mean the default endpoint. A missing saved device
  falls back to the default, as on Linux.
- **MIDI**: WinMM first (works on every Windows version; SysEx via
  `midiOutLongMsg`; no hotplug notifications, so poll the port list at a
  low rate; ports are exclusive per process). Windows MIDI Services can
  replace it later.
- CI builds all of these; nothing can run against real devices there -
  `NullAudio` covers the `Player` loop.

## Step 5 - `--headless` on Windows

With steps 1-4, `HeadlessUI` (no terminal) should work: play a song, MIDI
input, sample recording, a Launchpad as the performance surface. This is the
first Windows build a musician can use, and still needs no UI decision.
Verify by hand with a Launchpad on Windows; document in `docs/headless.md`.

## Step 6 - Installer

- **Inno Setup**, built on CI from tags.
  - It bundles the MSYS2 runtime DLLs the binary needs (collected with
    `ntldd`), the default song, a General MIDI SoundFont (FluidR3_GM, MIT),
    a SOFA file if its licence allows, and the licence texts.
  - Start menu entries for the terminal UI (once it runs on Windows) and
    `--headless`; the window gets one in phase 2.
  - Later: an MSIX or winget manifest.
- **Code signing:** an unsigned installer triggers SmartScreen warnings. A
  signing service (Azure Trusted Signing or similar) runs in the release
  job once there are users to protect.
- **Linux:** AppImage or Flatpak later; distro packages need no installer.

## After this plan

The graphical UI: `plans/cell-ui-window.md`, once the Cell UI's terminal
phase has separated the UI.

Step 4 notes: the backends are in `synth_engine` on Windows (`WasapiAudio`,
`AudioDevicesWasapi`, `WinMmMidiInput`, `WinMmMidiPorts`) and compile in CI,
but have not run against real devices. The output rate is the device's mix
rate, adopted like ALSA's negotiated one; a Launchpad is not recognised on
Windows until its WinMM port names are added to `modelFromDeviceName()`.
