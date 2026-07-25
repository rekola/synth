# DirAC-style directional-analysis heatmap scope

## Context

The terminal UI currently shows an FFT/spectrum chart and a per-channel loudness meter (`UI::layout()`, `UI.cpp:73-87`), but nothing shows *where in space* sound is actually arriving from once it's been ambisonic-encoded — there's no way to visually confirm that a track's configured `azimuth`/`elevation`, or a bus effect's spatial spread (reverb taps, delay taps, granular grains), is actually landing where intended in the encoded signal, short of trusting the math or listening carefully. This plan adds a live directional heatmap (DirAC-style: per-band active-intensity direction + diffuseness, energy-weighted) tapped from the shared ambisonic bus, rendered in braille next to the channel-loudness meter. It also adds legends to this new heatmap and to the existing FFT chart, which has none today.

Note: an earlier draft of this plan also added a secondary "SH energy strip" readout (raw per-channel |W|/|X|/|Y|/|Z| magnitude bars) specifically to disambiguate one known estimator limitation (§3) — W-only content and true diffuse content both read as diffuseness≈1. That secondary readout is dropped from this plan for now (no extra widget, no extra layout rows); the ambiguity itself still exists and is documented in §3, just without a built-in UI disambiguation mechanism yet. `DiracAnalyzer` still computes per-channel band magnitudes internally (cheap, already needed for the energy term) so a future pass can expose them without redoing the analysis math.

Two codebase facts drive most of the design below (confirmed by direct file inspection, not assumption):
- **No lock-free ring exists today.** The only cross-thread transport is `EventQueue` (`EventQueue.h`) — a `std::mutex`-guarded `std::deque` with a `socketpair` wakeup fd — and it currently carries only already-reduced scalars (the FFT dB vector, per-channel loudness) from the audio thread to the UI thread, never raw multichannel audio. A new, narrow, purpose-built lock-free ring is the minimal extension needed to carry raw bus blocks without doing FFT work on the audio thread.
- **No third "scope" thread exists**, just audio (`Player::play()`) and UI/main (`TerminalUI::startUI()`). Given the CPU estimate below comes in at ~1.5% of one core worst-case, the analysis runs on the existing UI thread, piggybacked on its existing poll-loop wakeup — no new thread.

## 1. Analysis parameters

| Parameter | Default | Why |
|---|---|---|
| Channels analyzed | W, Y, Z, X only (raw indices 0-3 of `Mixer::getRawBus()`, regardless of ambisonic order) | Per spec, first order suffices; Acn4-8 ignored. At order 0 (MONO) only W exists — Y/Z/X treated as 0, same nullptr-tolerant convention `decodeToStereo` already uses. |
| STFT size N | 1024 samples (23.2ms @ 44.1kHz) | Power-of-2 for FFTW; standard DirAC analysis-window range — long enough for usable low/mid-band bin counts, short enough to track transients/panning changes. |
| Hop | 512 (50% overlap), Hann window | Standard overlap-add STFT convention; avoids the boundary discontinuities a rectangular window would inject directly into the intensity vector. Internal analysis rate: 44100/512 ≈ **86.1 Hz**. |
| Bands | 8, doubling ladder: 20-150, 150-300, 300-600, 600-1200, 1200-2400, 2400-4800, 4800-9600, 9600-20000 Hz | Per-bin direction estimates are far too noisy to display; a handful of bands gives real spectral/spatial separation (e.g. hi-hat vs. bass direction) without bin-level flicker. Band 0 has only ~3 usable bins at N=1024 — flagged as unreliable (§3), not excluded, so the display never silently hides content. |
| Per-band smoothing (I, E) | One-pole IIR, τ≈100ms | Damps 86Hz-frame noise in the raw estimates themselves, independent of display ballistics. |
| Heatmap grid ballistics | Attack τ≈15ms, release τ≈400ms (asymmetric, VU-meter style) | Fast pop-in, slow fade — reads as a trail, not a flicker. |
| UI delivery / redraw | ~28.7Hz (every 3rd analysis frame; simple `counter % 3` gate, no new timer) | Terminal redraw/color-mapping cost buys nothing above this; matches existing "throttle via a modulo on an already-firing wakeup" idiom rather than adding a wall-clock timer. |
| Logical direction grid | 36 azimuth bins (10°) × 18 elevation bins (10°) | Decoupled from the actual widget's braille/pixel resolution, resampled at render time — same pattern `Chart::displayFFT()` already uses to bin an arbitrary FFT vector down to however many columns the widget has. |

Why not reuse `dsp/FFT.h`: it's purpose-built for `Player::fft_`'s single ~10Hz snapshot (fires once its ring fills *and* ≥50% is new data since last `reset()` — `FFT.h`'s `addData()`), sums L+R into one mono signal, and applies no window — wrong shape (need 4 independent channels) and wrong for a continuous 50%-overlap STFT (no window would corrupt the intensity vector at block boundaries). `dsp/DiracAnalyzer` needs its own small FFTW wrapper, dependency-free like `FFT.h`/`dsp/ChorusEngine.h`, but not built on top of either.

## 2. Tap point, transport, and threading

**Tap**: `Player.cpp:219`, immediately after `state_.render(...)` and before `mixer->encode()` — the exact point `mixer->getRawBus()` is already read one line later for the volume meter (`Player.cpp:227-229`). This is definitionally post-all-encodes (every track's `AmbisonicVoiceEncoder::encodeBlock` plus the shared send bus's own ambisonic accumulation, both already summed into the bus by `SongState::render()`), pre-decode.

**New primitive — `SpscBlockRing`** (`SpscBlockRing.h`, root level next to `EventQueue.h` — a threading primitive, not DSP math): a generic lock-free single-producer/single-consumer ring of preallocated fixed-size float blocks (`configure(block_floats, capacity)` once; producer `acquireWriteBlock()`/`commitWriteBlock()`, never blocks, drops silently if full since this is a visualization feed, not audio-critical; consumer `acquireReadBlock()`/`releaseReadBlock()`, never blocks). Block = 4 channels × `audio.getFrameCount()` frames; capacity 16 blocks (~93ms of buffering at a 256-frame period — ample headroom against UI-thread scheduling jitter; audio thread never waits). Owned by `Controller` (`Controller.h`, alongside the existing `EventQueue` members) since both threads touch it.

**Producer**: `Player.cpp`, right after the existing tap — copy the bus's first 4 channels (guarding `numberOfChannels() < 4` the same way `decodeToStereo` already guards a missing Y channel) into `acquireWriteBlock()`/`commitWriteBlock()`.

**Consumer — no new thread**: drained from inside `TerminalUI::startUI()`'s existing poll loop, piggybacked on the same wakeup that already drains `PlaybackEvent`s (`TerminalUI.cpp:551-559`), which fires at (at least) the audio-block rate. A new `UI::updateDiracHeatmap()` method (platform-agnostic, alongside `handlePlaybackEvent()`) drains every currently-available ring block, feeds each to a `DiracAnalyzer` member, and on every 3rd analysis frame pushes the resulting grid into the heatmap widget. `EventQueue`/`PlaybackEvent` are untouched — they keep carrying only the existing FFT/loudness scalars.

## 3. Known estimator limitations (must appear in the scope's own footer legend, not just this doc)

- **Concurrent sources in a shared band smear into one blended phantom direction**, not two hotspots — a fundamental first-order-only limitation, and deliberate here per spec ("first-order components suffice").
- **W-only content and true diffuse field both read as diffuseness≈1 — a real ambiguity, not a bug.** If X=Y=Z=0 (an unpositioned/`distance<=0` track, which `computeAmbisonicGains` already encodes as W-only), I=0 exactly as if the field were genuinely diffuse. In principle this is disambiguable — true diffuse content has real nonzero |X|,|Y|,|Z| magnitudes individually (just decorrelated from W, so they cancel in the intensity cross-product), while W-only content has |X|≈|Y|≈|Z|≈0 — but this plan does not add a UI readout for it yet (dropped for now, see Context). Footer text should still say so plainly: `"haze = reverb OR unpositioned source — cannot be told apart yet"`.
- **Band-level resolution** blends two different frequencies in the same band arriving from different directions into one estimate.
- **Band 0 (20-150Hz)** has only ~3 usable bins at N=1024 — visually deprioritize (lower max weight) rather than silently hide.
- **Early reflections read as spuriously directional** right after a sharp attack, before a reverb tail decorrelates into a true diffuse field — expect brief low-diffuseness blips even from mostly-reverb sources immediately after transients.

## 4. DirAC math

Direction: `az = atan2(I_y, I_x)`, `el = atan2(I_z, sqrt(I_x²+I_y²))`. Verified algebraically against `computeAmbisonicGains` (`AmbisonicEncoding.h:77-99`, azimuth convention confirmed against `tests/AmbisonicEncodingTests.cpp`'s `ambisonic_gains_right_is_positive_y`): for a single coherent point source, `I = [g_Y, g_Z, g_X]·|s|²`... i.e. `atan2(g_Y,g_X) = atan2(sin(az)cos(el), cos(az)cos(el)) = az` exactly (the `cos(el)` factor cancels in `atan2`), likewise for elevation. **This axis mapping is the single most important correctness detail in the whole feature** — get it backwards and the heatmap silently, systematically misaligns with real encoded positions without crashing or looking obviously wrong. It gets a dedicated unit test (§6, test 1).

Intensity: `I = Re{conj(W)·[X,Y,Z]}`, summed per-bin within each band (summing complex values then taking `Re` is mathematically identical to the reverse order, since `Re` is linear).

**Energy — resolved, not left as a free constant**: `E = ½(|W|² + |X|² + |Y|² + |Z|²)`, each term summed per-bin (never sum complex bins before taking magnitude for *this* term — bins at different frequencies have unrelated phase, so that would inject spurious cancellation). The ½ is required, not optional, for *this* engine's specific SN3D/AmbiX convention (unity-gain W, `kAmbisonicReferenceGain=1.0`): for a single point source, `|X|²+|Y|²+|Z|²` always equals `|W|²` identically regardless of direction (both equal `|s|²`) — so *without* the ½, `E=2|s|²` against `|I|=|s|²` gives diffuseness=0.5 for a perfectly dry, correctly-positioned source (wrong); *with* the ½, `E=|s|²`, diffuseness=0 (correct). Getting this wrong wouldn't crash anything — it would just make every dry voice silently read as ~50% diffuse. This is the load-bearing unit test (§6, test 1, checks diffuseness < 0.05 specifically, not just "low").

Diffuseness: `ψ = 1 − |I| / max(E, ε)`, clamped to [0,1].

## 5. Rendering mapping

Per band, per analysis frame: `directional_energy = E·(1−ψ)` bilinear-splatted into the nearest logical grid cells at (az,el) (4-cell splat, O(1)); `diffuse_energy = E·ψ` accumulated as one smoothed scalar *per band* (8 numbers, not spread over all 648 cells every 86Hz frame — wasteful). At render time (~28.7Hz): `displayed[cell] = smoothed_directional_grid[cell] + Σ_band(diffuse_energy_smoothed[band]) / 648` — i.e. the diffuse haze is spread uniformly across the whole grid only once, at the throttled render rate.

Per-cell color: fixed hue (reuse `StyleProvider.h`'s existing `command_column_color = "#c67610"` for UI-wide consistency), brightness = normalized log-energy at that cell, **saturation = directional_energy_at_cell / (directional_energy_at_cell + local diffuse contribution)** — concentrated hotspots render fully saturated, cells reached only by the haze layer render desaturated/gray. This directly implements "hotspots vs. desaturated haze" (both saturation-lowering *and* spreading, since they're complementary, not either/or, given the split above).

Decay: the attack/release ballistics from §1 apply to the logical grid every analysis frame (86Hz), independent of the render throttle.

**Marker overlay**: no existing "marker" layer exists anywhere in this UI (confirmed by search — `InstrumentTrack::getAzimuth/getElevation` are pure data; `LaunchpadManager::azimuthToRow/rowToAzimuth` only drive physical Launchpad LEDs, not a terminal visualization). New: one glyph per currently-active track, plotted at its static configured `InstrumentTrack::getAzimuth()`/`getElevation()` (gated by `PlaybackInfo`'s active-voice check) on the **same** 36×18 az/el projection as the heatmap grid — critical that both use the identical projection or the overlay would misalign independent of whether the heatmap itself is correct. One marker per track for v1 (not per-voice/per-unison-spread) — sufficient for the verification checklist's needs; deeper per-voice marker plumbing isn't justified yet.

## 6. Worked CPU estimate

At the defaults above (N=1024, hop=512 → 86.1 frames/s, 4 channels, 513 bins, 8 bands, 36×18=648 cells, 28.7Hz render):

- FFT: real-FFT ≈ 2.5·N·log2(N) flops/channel/frame = 2.5·1024·10=25,600 × 4 channels × 86.1/s ≈ **8.8 MFLOPs/s**.
- Per-bin intensity+energy: ~32 flops/bin × 513 bins × 86.1/s ≈ **1.4 MFLOPs/s**.
- Grid decay+splat: 648 cells × 86.1/s × ~3 flops + 8 bands × 86.1/s × ~50 flops (2×atan2+splat) ≈ **0.2 MFLOPs/s**.
- Render-time color mapping (worst case, pixel-graphics mode, ~200×80px): 16,000px × 28.7Hz × ~10 flops ≈ **4.6 MFLOPs/s**.

**Total ≈ 15 MFLOPs/s sustained**, against even a conservative 1 GFLOP/s single-core budget: **~1.5% of one core worst case, <1% typical** — and entirely on the UI thread, not the real-time audio thread, so zero audio-glitch risk regardless of this number.

## 7. Test plan

New `tests/DiracAnalyzerTests.cpp` (registered in `tests/CMakeLists.txt`), pure DSP-level tests against `dsp/DiracAnalyzer.h` directly — no `SongState`/`Mixer`/threading, same philosophy as `AmbisonicEncodingTests.cpp` testing `computeAmbisonicGains` directly:

1. **`dirac_single_source_is_low_diffuseness_at_correct_direction`**: synthesize a signal, apply `computeAmbisonicGains(SphericalPosition{az,el,1})` directly (the exact same encoder the real render path uses) to get W/X/Y/Z, feed through `DiracAnalyzer`, assert estimated direction within a few degrees and **diffuseness < 0.05** (not just "low" — this specific threshold is what catches the ½-weight energy bug from §4; without it this test would read ~0.5, not ~0).
2. **`dirac_decorrelated_eight_directions_is_near_full_diffuseness`**: reuse `cubeVertexDirections()` (`AmbisonicEncoding.h`) — 8 independent noise sources, one per cube-vertex direction, summed — assert diffuseness > 0.9.
3. **`dirac_w_only_is_also_near_full_diffuseness`**: W-only content (X=Y=Z=0) — assert diffuseness *also* > 0.9, checked in as an expected result documenting the known ambiguity from §3 (not a surprise): this is the same diffuseness reading as test 2's genuinely-diffuse case, on purpose. `DiracAnalyzer`'s internal per-channel magnitudes (kept even without a UI readout, see Context) can additionally assert |X|,|Y|,|Z| are near 0 here vs. nonzero in test 2, proving the data needed to disambiguate is already available internally even though nothing displays it yet.

New test in `tests/RenderTests.cpp`: **`render_dirac_heatmap_peak_matches_encoded_azimuth_sweep`**, reusing the existing `tests/fixtures/ambisonic_directions.xml` fixture (already sweeps azimuth every 45° around the horizon plus straight up/down, broadband pink noise — ideal, since it excites every proposed band rather than just one). Drive it with the `SongState`+real-`AmbisonicStereoMixer` pattern `render_send_a_is_distance_invariant` already establishes (**not** the locally-defined `RecordingMixer`, whose `getRawBus()` is a stubbed-empty `SampleData` and can't be used here): loop `state.render(256, song, mixer)`, feed `mixer.getRawBus()` into `DiracAnalyzer` each iteration, and per each track's known firing window (same row-based windowing `render_ambisonic_directions_produce_distinguishable_output` already uses) assert the heatmap's peak direction is within ±15-20° of that track's configured azimuth/elevation (tolerance accounts for the 10°-bin grid plus smoothing lag).

## 8. Manual verification checklist

1. Normal song playing, dry positioned voices: each active voice appears as a bright, saturated hotspot at its marker overlay position.
2. Reverb-only content (e.g. `sendMain=0`, `sendA` active — same fixture pattern as `render_send_main_zero_silences_main_channels_but_not_sends`): display shows a spread, desaturated haze, no concentrated hotspot.
3. Deliberate marker/heatmap mismatch sanity check (one-off manual QA): temporarily hardcode a fixed azimuth offset into the marker overlay's read path, play a fixture with a known hard-panned dry source, confirm the **heatmap hotspot does not move** with the marker — proving the heatmap measures real bus content rather than mirroring the track's configured value — then revert.

## Files

**New:**
- `dsp/DiracAnalyzer.h`/`.cpp` — STFT (own FFTW plan, Hann window, N=1024/hop=512, 4 channels) + per-band intensity/diffuseness + 36×18 logical direction-grid accumulator with attack/release ballistics + per-band diffuse-haze scalars. Takes `const SampleData&` blocks (W/Y/Z/X), matching `dsp/ChorusEngine.h`'s `SampleData`-based precedent, not raw pointers.
- `SpscBlockRing.h` — generic lock-free SPSC ring of preallocated fixed-size blocks (root, next to `EventQueue.h`).
- `HeatmapChart.h` — small abstract base with a 2D-grid contract (`setCell`, marker list, `commit()`), kept separate from `Chart`'s 1D `setSample`/`displayFFT` contract rather than folding 2D methods into it.
- `TerminalHeatmapChart`/`TerminalPixelHeatmapChart` — local classes inside `TerminalUI.cpp`, same convention as the existing `TerminalChart`/`TerminalPixelChart`, reusing the same pixel-support switch.
- `tests/DiracAnalyzerTests.cpp`, registered in `tests/CMakeLists.txt`.

**Modified:**
- `Player.cpp` — configure `SpscBlockRing` once at startup (next to the existing `fft_.setSize(...)` computation); push a 4-channel bus copy after the existing `mixer->getRawBus()` tap.
- `Controller.h` — add the `SpscBlockRing` member + accessor.
- `UI.h`/`UI.cpp` — add `heatmap_` (`shared_ptr<HeatmapChart>`), a `DiracAnalyzer` member, a new `updateDiracHeatmap()` method; extend `layout()`.
- `TerminalUI.cpp` — construct the new widget in `initialize()` (same `make_chart`-style lambda pattern); call `updateDiracHeatmap()` from `startUI()`'s poll loop right after the existing `PlaybackEvent` drain; call `chart_->setFooterLabel(...)` once for the FFT legend (this is the entire "add a legend to the FFT scope" ask — `Chart::setFooterLabel()` already exists and both renderers already reserve a row for it, so this is a one-line addition, not new rendering code).
- **Layout** (`UI::layout()`) grows the shared top area from 4 to **5** rows — one extra row for a shared legend (heatmap + FFT), reusing the existing `setFooterLabel()` mechanism both widgets already support:
  ```cpp
  constexpr int kHeatmapWidth = 20; // ~9°/dot azimuth resolution in braille mode
  chart_->resize(4, cols - 6 - kHeatmapWidth).move(1, 0);
  heatmap_->resize(4, kHeatmapWidth).move(1, cols - 6 - kHeatmapWidth);
  volume_meter_->resize(4, 6).move(1, cols - 6);                 // unchanged
  pattern_editor_->resize(rows - 8, cols).move(6, 0);             // was rows-7 / row 5
  ```

## Verification

1. `cmake --build build -j` clean, `ctest --test-dir build --output-on-failure` 100% pass including the new `DiracAnalyzerTests.cpp` and the new `RenderTests.cpp` sweep test.
2. Run the interactive `musiceditor` with a real song, confirm the manual checklist (§8) items 1-3 by eye.
3. Re-run `render_send_a_is_distance_invariant`/other existing spatial regression tests to confirm no accidental interaction with the new tap (it's read-only against `mixer->getRawBus()`, so this should be a formality, not a real risk).
