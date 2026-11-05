---
paths:
  - "src/ambisonic/**"
  - "src/bus/**"
  - "src/effects/**"
  - "src/dsp/**"
  - "src/audio/**"
  - "src/midi/**"
  - "src/instruments/**"
  - "src/state/**"
  - "src/playback/**"
  - "src/util/Notifier.*"
  - "src/util/WaitSet.*"
  - "tests/RenderTests.cpp"
---

# Audio engine: devices, spatial mixing, bus, effects

## Devices

- Output, input and one MIDI source are chosen at runtime
  (`select-playback-device`/`select-capture-device`/`select-midi-input`, the
  Devices menu; a modal `UI::showChoiceDialog()` over the toolkit-agnostic
  `ui/ChoiceList.h` — the commands live in `UI`, a backend only supplies the
  dialog) and saved in `~/.config/synth/devices.conf`
  (`audio/DeviceSettings.h`, in `synth_engine`; machine-wide, never in a
  song). `--playback-device`/`--capture-device`/`--midi-input` override one
  run; `--list-devices` prints the choices. `docs/devices.md`.
- An audio name is `""`/`default`, `pw:<node.name>` (stable across sessions,
  unlike numeric ids) or a raw ALSA PCM name. A `pw:` device is opened
  through ALSA's pipewire plugin with a private one-PCM config
  (`openPcm()`), so period sizes, polling, delay queries and xrun recovery
  stay the one ALSA path for every device.
- The audio thread owns the PCM handles, so `Controller::setPlaybackDevice()`
  /`setCaptureDevice()` save the choice and push `SET_PLAYBACK_DEVICE`/
  `SET_CAPTURE_DEVICE` for `Player` to make the switch. A switch that can't
  happen (node gone, rate/block size mismatch, capture in use) logs why and
  keeps the current device. MIDI is read on the UI thread, so
  `UI::selectMidiInput()` connects there; an unplugged source reconnects when
  its port reappears. A saved device missing at startup falls back to the
  default, keeping the choice. A nonexistent node name is rejected up front
  because the server silently substitutes the default input; capturing a
  sink's monitor is unsupported for the same reason. A missing ALSA sequencer
  never stops audio — MIDI is just off.
- `AudioDevices.cpp` lists the server's own nodes with libpipewire when
  built with it (dialog title "(PipeWire)"), else sound cards via the ALSA
  control API, one entry per card input/output — never
  `snd_device_name_hint()`'s dozens of virtual PCMs ("(ALSA sound cards)").
  Identical labels are told apart by `device.bus-path` cut to the USB port
  (`DeviceLabels.h`), numbered as a last resort.
  `tools/e2e/verify_device_selection.py` drives the pickers against a real
  PipeWire and skips without one.

## Waiting

- Nothing outside a backend sees the device's descriptor: `Player` calls
  `AudioAPI::wait(notifier, want_capture, timeout)` and gets a `WaitResult`
  (woken/playback/capture). The wakeup is `util/Notifier.h` (level-triggered;
  eventfd on Linux, an event on Windows, a pipe elsewhere), raised by
  `MessageQueue` while it holds messages. An unstarted capture stream is
  "ready" forever, so capture is masked off while not wanted.
- `NullAudio` (`--null-audio`) takes blocks at the clock's pace with no
  device, so the real `Player` loop runs in CI and tests.
- MIDI input (`midi/MidiInput.h`, read on the UI thread) and Launchpad ports
  (`midi/MidiPorts.h`) are not part of `AudioAPI`. UI loops are cellui
  `EventLoop`s with `util/WaitSet.h` groups as sources (`ui/WaitSources.h`);
  each source adds itself (`addTo()`) and is read when its group is ready.

## Ambisonic bus and binaural decoding

- Every song is rendered through an ambisonic bus (ACN/SN3D, AmbiX). There
  is no plain-stereo mixer type and no `--mono`; `--ambisonic [order]` sets
  the order 1-3 (`kAmbisonicOrder`; 3rd is a hard ceiling), and omitting it
  or the number defaults to 3. `MONO` (0th order, W only) survives only as the
  internal shape voices and nonlinear per-track effects reduce to, plus one
  synthetic test; every mixer still decodes to a 2-channel device signal
  (`getDeviceChannels()` is 2; `decodeToStereo()` broadcasts a W-only bus).
- The bus is decoded to binaural (libmysofa, when `SYNTH_ENABLE_BINAURAL` and
  a SOFA file resolves) or a cardioid stereo matrix. `--stereo` and
  `toggle-mixer-type` force the cardioid decoder (`MixerType::
  AMBISONIC_STEREO`); the order is unaffected. `MixerFactory` picks between
  `AmbisonicStereoMixer`, `AmbisonicBinauralMixer` and `AmbisonicMagLSDecoder`
  for both `Player` and `OfflineRenderer`, so they share one selection path.
- Two binaural decoders, chosen by `Controller::getUseLegacyBinaural()`:
  - **MagLS** (default): one fixed filter pair per ambisonic channel,
    solved once at load against the whole measured HRTF grid —
    phase-accurate least squares below 1.5 kHz (preserving ITD),
    magnitude-only above with phase propagated from the previous bin's
    reconstruction, plus a diffuse-field covariance constraint. Uses
    `RealFFT<float>` directly.
  - **Legacy** (`--legacy-binaural`): a virtual-loudspeaker rig — an
    8-speaker cube at order 1, a 12-point icosahedron at order 2, a 26-point
    Lebedev grid at order 3, max-rE weighted, each speaker convolved with a
    measured HRIR pair. max-rE is exclusive to this rig. Its `gain_trim_` is
    derived from the loaded HRIR set's L2 energy times `kGainTrimTarget`, so
    swapping SOFA files can't silently reintroduce clipping.
  Both find the SOFA file through `SofaFileResolver.h` (project-local
  `data/` first, then `~/.local/share/sofa/default.sofa`, the same order as
  the SoundFont lookup) and fall back to cardioid if none resolves.
- `AmbisonicEncoding.h` is the one source for encode/decode math and
  `cubeVertexDirections()` (az ±45°/±135°, el ±35.264°), used by the legacy
  order-1 rig and the send bus's reverb taps alike — never a second copy of
  those constants.

## Buffers, sends and structural silence

- `AudioBuffer`'s `Channel` enum is `Main`/`AuxA`/`AuxB`. `Main` is the group
  of regular ambisonic channels, addressed by raw ACN index; `hasChannel(Main)`
  is derived (`regularChannelCount() > 0`), never stored, so no operation can
  leave it stale. `AuxA`/`AuxB` sit after the regular channels, `AuxA` first,
  each with its own presence bool; a buffer may carry either, both or neither
  regardless of regular count. A voice with nothing routed to Main this block
  has zero regular channels — not allocated-then-zeroed. `getChannel(Main)`
  is channel 0 or `nullptr`. `isClipping()`/`calculateLoudness()` scan
  whatever channels are present (a 100 %-wet voice has clippable aux and no
  Main). `mixNamed()`/`assignNamed()` are the aux-tolerant siblings of
  `mix()`/`assign()`: an aux channel present on one side only is ignored.
- A track *sends* A and B (`sendA`/`sendB`, any instrument type); what
  arrives on the bus is carried in `AuxA`/`AuxB`. A `SoundFontVoice` adds its
  SF2 region's reverb/chorus send generators (15/16) additive-then-clamped,
  SF2's own merge convention. A voice with Send Main = 0 skips Main entirely
  (`encodePosition()` checks `sends.main > 0`, as the aux presence checks do).
- An accumulator's shape is decided by rendering every child first and
  checking the real results' channels, never by a separate prediction.
  `InstrumentTrackState::render()`'s chunked loop collects each chunk's
  buffer, then builds the accumulator from their union, so a voice starting
  mid-block with a new aux channel is captured in that block. Aux channels
  reach each `Mixer`'s accumulator via `mixNamed()` but no `Mixer::encode()`
  reads them.
- **Silence is structural.** When nothing sounds, buffers carry no channels
  instead of zeros, so no stage tests samples for silence. A `Mixer` notes
  whether `accumulate()` added any regular channel this block
  (`accumulated()`); until it has, `getRawBus()` is a frame-sized buffer with
  no channels. `encode()` with nothing accumulated only drains the filter
  tail and still writes a full stereo block of zeros — the device needs a
  continuous stream. `SongState::getAuxASum()`/`getAuxBSum()` are empty unless
  a track sent. Silence is made at the sources: a voice ends below
  `constants::SILENCE_KILL_FLOOR_DB`; the send bus leaves the mix once its
  output stays under `kBusAudibleFloor` (-100 dBFS). `VisualizationThread`
  feeds real zero blocks until the spectrum and DirAC analysers report
  `atRest()`, then sends only meter readings, then after `kSettleSeconds`
  nothing, so an idle UI sleeps; a second later `Player` stops sending silent
  blocks. The UI blanks the scopes itself when they come back on screen
  (`InteractiveUI::scopes_stale_`).

## Send bus (`src/bus/`) and per-track effects (`src/effects/`)

- `src/bus/` depends on `src/dsp/` (reusable, dependency-free DSP blocks),
  never the reverse. Per-track effects are constructed per track/note and
  torn down with it; the bus is shared. The only reverb is the bus's spatial
  FDN (`bus/FDNReverb.h`); `<reverb>` only ever means that one, as a `<bus>`
  child.
- `SongState::render()` sums `AuxA`/`AuxB` off every top-level track into two
  persistent mono accumulators and (only when the config is `AMBISONIC`) runs
  them through `SendBusProcessor::process()` — silent input included, so
  every slot's internal state stays continuous — until input and output have
  stayed under `kBusAudibleFloor` longer than either effect keeps a sound
  (`BusEffect::idleAfterSeconds()`: its longest delay or ring-down; never for
  an effect that doesn't say, or a frozen granular cloud). The result is
  accumulated into the mixer with one `accumulate()` call while audible
  (`isAudible()`: block peak reached the floor within
  `kBusAudibleHoldSeconds`; the floor sits far above the effects'
  `kDenormalGuard`). The bus output is always ambisonic-shaped, never stereo.
- `SendBusProcessor` is a generic 2-slot chain (`kSlotA`/`kSlotB`), resolved
  once at song load from the file or `BusEffectRegistry`'s default (A =
  `FDNReverb`, B = `MultiTapDelay`; `GranularCloud` and `NullBusEffect` also
  exist, and both slots are `Null` before `initialize()`, so `process()` is
  always safe). Every `BusEffect` exposes N spatial taps
  (`getNumTaps()`/`getTap()`/`getTapDirection()`: FDN 8 at the cube vertices,
  delay 4 fixed azimuths, cloud one per grain), each encoded by its own
  `AmbisonicVoiceEncoder` at `getWetLevel()`. Slot B runs first; its
  pre-encode tap sum scaled by `getChainSendLevel()` feeds slot A's input in
  the same block. Slot A's chain-send ratio exists uniformly but is unread.
- `dsp::ChorusEngine` backs only the per-track `Chorus` effect
  (`voices`/`rate`/`delay`/`depth`/`mix`), with `decorrelate = false` so a
  silent channel stays silent, and processes Main and `AuxA`/`AuxB` through
  independent always-present state.
- Nonlinear per-track effects (`Chorus`, `Distortion`) reduce their children
  to `MONO` before rendering (`reduceForEffect`) — panning deliberately
  doesn't survive under them — and re-encode with `encodeMonoAsPoint()` (W
  only, unity), carrying aux through unencoded; `encodeStereoAsPoints()` is
  only for genuine 2-channel input. Every per-track effect shapes whatever
  channels are present, Main and aux alike (the bus should hear the same
  envelope/gain/tone as the dry signal), except Compressor's *detection*,
  which uses Main only. Persistent per-channel state (`Biquad<T>`,
  `dsp::MoogVCF<T>`, `ChorusEngine::ChannelState`) gives aux its own slots
  rather than reindexing by position (Main toggles between 0 and full, never
  partial), and once a channel has carried data its state keeps advancing
  through silence (the no-buffer `apply(blockSamples)` overloads) so it
  decays and resumes correctly. Known, not fixed: `Distortion` can distort
  Main and aux differently under clipping (`docs/known_bugs.md`).

## Instruments and voices

- Instruments resolve from a GM SoundFont found by `findDefaultSoundFont()`:
  project-local `data/FluidR3_GM.sf2` (gitignored) first, then well-known GM
  fonts in `~/.local/share/{soundfonts,sounds/sf2}`, `/usr/share/soundfonts`,
  `/usr/share/sounds/sf2`, then the largest `.sf2` there. Ubuntu's
  `fluid-soundfont-gm` provides FluidR3_GM.sf2. Without any SoundFont,
  `InstrumentProvider` still builds one built-in fallback (a generic sawtooth
  `Oscillator`) that `getInstrumentByName()` returns for any unresolved name;
  per-path fallbacks (an FM piano for `piano.*`) are a plan, not code.
- An `Oscillator` is an array of up to 256 members (`OscillatorArray.h`,
  rendered by `OscillatorVoice` with `OscillatorKernel`'s vector-extension
  waveforms): member k at `ratio`^k times the frequency and `falloff`^k times
  the level, `detune` cents spread evenly and centred. `spread` (a multiplier
  on the position's extent) is the radius of a cloud of buckets — as many as
  resolvable cells fit its area (25/16/12° at order 1/2/3), at least three
  and at most one per member — laid out as concentric rings jittered per
  note; members are dealt round-robin, each bucket summed and encoded once
  (`AmbisonicStackEncoder`). Floor reflection and aux sends run once at the
  centre. A detuned array gets slow aperiodic pitch drift per member
  (`PitchDrift.h`: hashed value noise, a pure function of the voice's age, so
  independent of block size) over `driftPeriod` seconds (default 2, 0 off).
- Where a note sits around its track's position is the track's `spatial`
  mode (`SpatialMode`, `docs/spatial.md`), passed through `playNote()`; every
  note has a `NoteCoordinate` (a live note's with a stand-in row). Default is
  the golden-angle spiral by column (`instruments/SpatialPlacement.h`);
  `SoundFontInstrument` adds the drum-kit table and key-range arc, and a
  region's own pan is always folded in.
- `dsp/RealFFT.h` is the engine's one FFT wrapper (fixed size, no per-call
  allocation; only `RealFFT<float>` is instantiated), over PocketFFT with
  `POCKETFFT_CACHE_SIZE` (plans would otherwise be rebuilt every call) and
  `POCKETFFT_NO_MULTITHREADING` (deterministic) defined before the include.
  `dsp/SpectrumAnalyzer.h` wraps it with ring-buffer accumulation and dB
  conversion for the live spectrum.
