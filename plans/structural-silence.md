# Prompt: structural silence in the mixer and the scopes

Paste this into a fresh session on this repository.

---

## Goal

When nothing sounds, the audio path should carry **structurally empty**
buffers instead of zero-filled ones, so no stage needs to test samples for
silence. The scopes then get their event every block like any other, and it
costs next to nothing.

## Background (measured, already fixed in a cheaper way)

After a clip had played, `synth` kept the audio thread at 24% and the
visualization thread at 15% with nothing sounding. The send bus's denormal guard
(`kDenormalGuard = 1e-20f` in `src/bus/FDNReverb.cpp`, and the same trick in
`src/bus/MultiTapDelay.cpp`) keeps a tiny nonzero signal flowing forever, and
the binaural decoders only skipped an exact `0.0f`, so they convolved it in full
every block. Two stop-gaps are in place:

- `AmbisonicMagLSDecoder::encode()` and `AmbisonicBinauralMixer::encode()` skip
  input below `kInaudible = 1e-9f`.
- `VisualizationThread::handleAudioBlockEvent()` counts blocks of silence
  (`silent_blocks_`, `silent_limit_`, from the loudness it already computes),
  skips its decode/FFT/DirAC after about a second, and sends one real result
  about four times a second so a scope still ends up empty.

This task replaces both with structure. **Remove the sample-level threshold in
the decoders, the `silent_blocks_` counter and the four-a-second heartbeat.** A
fake periodic event is not wanted: the structurally empty event is sent every
block, as every other event is.

## Design

`AudioBuffer` already has the vocabulary (read `src/audio/AudioBuffer.h` and the
`Channel` paragraph in `CLAUDE.md`): `hasChannel(Channel::Main)` is derived from
`regularChannelCount() > 0`, `AuxA`/`AuxB` exist only when present, and a voice
or track with nothing to say simply has zero regular channels. The gap is
further up, where buffers are given their full shape regardless:

1. **The send bus.** `SongState::render()` sums `AuxA`/`AuxB` off every
   top-level track into two persistent mono accumulators and always runs
   `SendBusProcessor::process()` (even on silence, so reverb tails and
   modulation stay continuous), then calls `mixer.accumulate()` once with the
   ambisonic result. Make the bus report whether its output this block is
   audible (above a floor, with a short hold so a tail is not cut), and when it
   is not, skip that `accumulate()` and do not add the bus result to the mixer.
   The bus must keep its internal state continuous; only its contribution is
   dropped. Decide where the floor lives (one named constant, with the
   reasoning in a comment) and make the denormal guard still do its job.
2. **The mixer.** `Mixer` subclasses (`src/ambisonic/`) keep a full-shape,
   zeroed accumulator (`buffer_`, returned by `getRawBus()`). Give the mixer a
   notion of "nothing was accumulated this block" (a flag set by
   `accumulate()`, cleared by `reset()`), and make `getRawBus()`'s consumers
   able to see it. Preferred: `getRawBus()` returns a buffer with zero regular
   channels (and no aux) when nothing was accumulated, so `hasChannel(Main)`
   is the test.
3. **The decoders.** `encode()` with nothing accumulated only drains the filter
   tail (`left_tail_`/`right_tail_`), then writes zeros. It must still output a
   full stereo block of zeros: the ALSA device needs a continuous stream.
   Keep sending it. Pausing or dropping the PCM and restarting on the next
   sound costs start-up delay and risks clicks and xruns for no gain once the
   memset is all that is left. The tail must be drained exactly as before, so
   there is no audible change.
4. **The event.** `Player.cpp` builds `active_raw_bus` (around the
   `AudioBlockEvent` push) zeroed at full shape. Build it structurally empty
   instead when the mixer accumulated nothing. `AudioBlockEvent` is still sent
   every block.
5. **The visualization thread.** With an empty raw bus it must produce an
   *empty* result quickly: zero channel loudness, a zero FFT and an empty DirAC
   grid, without decoding or analyzing. The analyzers still need to be fed
   enough zeros after sound stops that they fall to zero (the FFT window is
   about 100 ms, DirAC has its own frames), so keep feeding them real zero
   blocks for as long as they need and then stop — decide that from their
   state, not from a wall-clock guess, and keep it simple. `VisualizationResultEvent` goes out
   every block; the UI's change detection (`TerminalUI::handleVisualizationResultEvent()`)
   already redraws nothing when the result equals the last one.
6. **Aux channels.** The same applies to `AuxA`/`AuxB` in the event and the
   mixer: absent when silent.

## Constraints

- Real-time thread: no allocation or locks added to `Player`/`SongState`
  render paths. A structurally empty buffer should be cheaper than a zeroed one.
- Everything must stay warning-clean (`-Werror`, `-Wsign-conversion`).
- Output must be sample-identical for any song that makes sound; verify with
  `--render` on a few songs before and after (`songs/`, `tests/fixtures/`),
  including one with reverb and one with a long release, and compare the WAVs.
  The `--render` path (`renderSongOffline()`) uses the same mixers.
- Do not change `Song`/document code. Follow `CLAUDE.md` conventions
  (comments short, no references to plans in code).
- Add unit tests: a bus that has decayed contributes nothing; a mixer with
  nothing accumulated returns a bus with no regular channels and an encode that
  drains its tail then yields zeros; a tail is not cut short; the visualization
  thread produces an empty result from an empty bus without decoding.

## How to measure

The sandbox has none of the real-world setup, so build it:

```sh
sudo apt-get install -y libmysofa-dev
mkdir -p ~/.local/share/sofa && cp /usr/share/libmysofa/default.sofa ~/.local/share/sofa/
cmake -B build-bin -DSYNTH_ENABLE_BINAURAL=ON && cmake --build build-bin -j4 --target synth
```

`--playback-device null` is not paced to real time, so idle numbers are
meaningless unless the audio loop is slowed. Temporarily (do not commit) add,
right after `audio.play(master, logger);` in `Player.cpp`:

```cpp
if (getenv("SYNTH_PACE")) std::this_thread::sleep_for(std::chrono::microseconds(
    static_cast<long>(audio.getFrameCount()) * 1000000L / audio.getFrequency()));
```

Then drive `synth` through a pty (`tools/e2e/harness.py`: `spawn`, `Screen`,
`wait_ready`; run it with `SYNTH_PACE=1`), launch a clip in Live View with
Enter, let it play, pause with Space, and read the per-thread CPU of the
`synth-ui`, `synth-audio` and `synth-visual` threads from
`/proc/<pid>/task/<tid>/stat` over ten seconds. Before this task the paused
state after playing costs audio ~24% and visualization ~15% with the old
stop-gaps removed; the target is ~0% on both, with no sample-level threshold
anywhere. Also check an untouched `synth` with no song, and a song that has
never played.

## Done when

- The decoders' `kInaudible` check, the visualization thread's silence counter
  and its four-a-second heartbeat are gone.
- Idle and after-playing CPU are ~0% on every thread in the measurement above.
- The scopes go empty after sound stops and show nothing stale when switched on
  later (toggle scopes, change view).
- `ctest` passes and `--render` output is unchanged for sounding songs.
- `CLAUDE.md`'s paragraph on `AudioBuffer`/the send bus describes the new rule
  (a silent mixer is structurally empty), and `docs/known_bugs.md` is updated if
  anything is left.
