# Fix the existing compile warnings

## Status

Stages 0, 0b, and 1 are done. Clean build now sits at **495 warnings**, down
from the original 564: 421 `-Wsign-conversion` + 74 `-Wsign-compare`, nothing
else. Stage 2 (`-Wsign-compare`) is next.

## Baseline (measured)

Clean `-DCMAKE_BUILD_TYPE=Release` build, `cmake --build . -j 20`, both the
engine lib and `synth_tests`: **564 warnings**, by flag:

| flag | count | notes |
|---|---:|---|
| `-Wsign-conversion` | 456 | explicitly enabled in `CMakeLists.txt` already |
| `-Wsign-compare` | 74 | implied by `-Wall` in C++ |
| `-Woverloaded-virtual=` | 11 | implied by `-Wall`; all 11 are the same site |
| `-Wunused-variable` | 6 | |
| `-Wreorder` | 6 | all one site (`Chart.h`), seen once per TU that includes it |
| `-Wmaybe-uninitialized` | 4 | one real site, `Song.cpp:433` |
| `-Wsign-promo` | 2 | one real site, `Song.cpp:529` |
| `-Wnarrowing` | 2 | one real site, `TerminalUI.cpp:40` |
| `-Wunused-but-set-variable` | 1 | |

`-Wsign-conversion` is concentrated: `Track.h`, `Song.h`, `AudioBuffer.h`,
`Note.h` and `PatternEditor.cpp` alone account for >80% of it (these are
widely-`#include`d headers, so a fix there collapses the count everywhere
they're pulled in). ~56 of the 456 are in vendored `third_party/tinyxml2/`,
which `CLAUDE.md` already says not to reformat/refactor.

## Should `-Wsign-conversion` just be muted?

No. `CLAUDE.md` already states the project's convention: *"The build enables
many `-Werror=` flags plus `-Wsign-conversion`; new code must compile
warning-clean."* Muting it would reverse a documented decision, not just a
build nuisance. It's also not just noise — `Player.cpp:58` compares a signed
`getInstrumentId()` against a `.size()` (a negative id would silently promote
to a huge unsigned value and pass the check), and `Song.cpp:433` has a real
`-Wmaybe-uninitialized` bug (`Command`'s `values_[]` can be read
uninitialized) sitting right next to the sign warnings. The existing 564
warnings should be fixed, not suppressed — but 456 sign-conversion sites is
too much for one pass, so stage the work below instead.

## Stages

**0. Vendored code — exclude, don't fix (free, zero risk) — done**
`third_party/` is now a `SYSTEM` include path
(`target_include_directories(synth_engine SYSTEM PUBLIC
.../third_party)`), and the two includes that reached it by relative/quote
path (`dsp/RealFFT.h`, `Song.cpp`) were switched to angle-bracket form so
they actually resolve through it — a quote-include resolved relative to the
including file's own directory doesn't pick up `-isystem` treatment.
`third_party/tinyxml2/tinyxml2.cpp` is compiled directly as its own
translation unit rather than merely `#include`d, so the `SYSTEM` include
path doesn't cover it either; it gets `-w` via
`set_source_files_properties(... COMPILE_OPTIONS "-w")` instead. Net effect:
`-Wsign-conversion` dropped from 456 to 422 (real measured delta, not the
originally-estimated ~56 — some vendored sites overlapped with counts
already attributed to files that include them).

**0b. Additional zero/near-zero-cost flags — done**
Measured every plausible extra `-W` flag against the real codebase before
touching `CMakeLists.txt` (`-fsyntax-only` over `compile_commands.json`).
Added as `-Werror=`, all now enforced:
`duplicated-cond`, `duplicated-branches`, `logical-op`, `redundant-decls`,
`cast-align`, `format=2`/`format`, `shadow`, `missing-declarations`. The
`-Wshadow` (4 sites: `Player.cpp` x2 — one nested loop counter, one nested
`event` local; `TerminalUI.cpp` x2 — one local shadowing a member, one
nested `event` local) and `-Wmissing-declarations` (2 sites — `UI.cpp`'s
`audio_thread_func`/`visualization_thread_func` had no prior declaration
and no external caller, so marked `static`) violations were fixed as part
of enabling them, since the whole point was landing at zero.

**`-Wnull-dereference` — deliberately not enabled.** The `-fsyntax-only`
check showed 0 hits, but that check doesn't run GCC's optimizer-based
analysis (only real at `-O2`+), so it missed the actual behavior. Enabling
it for a real build surfaced two problems, neither a real bug: (1) false
positives *inside vendored code* (`pocketfft_hdronly.h`, silenced with a
`#pragma GCC diagnostic ignored "-Wnull-dereference"` push/pop wrapped
around its one `#include` site in `RealFFT.h`, not the vendored file
itself), and (2) after that, false positives *inside libstdc++ itself* —
`InstrumentTrackState.h`'s `for (auto & [column, voices] : voices_)` over a
plain `unordered_map`, and `FDNReverbTests.cpp`'s ordinary `vector`
indexing, both misattributed through inlined STL container code at `-O2`.
Since even ordinary, correct project code triggers it, it isn't a viable
`-Werror=` (or even a useful warn-only) flag for this codebase on GCC 13 —
left off entirely, with the reasoning recorded as a comment next to the
flags line in `CMakeLists.txt` so it isn't rediscovered and re-tried blind.

**1. Real bugs and one-off sites — done**
- `Command.h` — `values_[4]` had no default member initializer, and the
  `string_view` constructor only assigned it when `values.size() >= 4`; in a
  Release build (`assert` compiled out) a malformed argument left it fully
  uninitialized. Fixed with a default member initializer
  (`= { '-', '-', '-', '-' }`) so every construction path is well-defined;
  `Command()` is now `= default`.
- `TerminalUI.cpp:40` — two narrowing conversions building `ncinput`; added
  explicit `static_cast<uint32_t>` at the `.id`/`.modifiers` initializers.
- `Chart.h` — reordered the init list (`min_y_, max_y_, type_`) to match
  declaration order, fixing `-Wreorder` at its one site.
- **`InstrumentTrackState.h` and `SongState.h`** — both had a derived class
  declaring its own differently-shaped `render()` that hid (rather than
  overrode) `TrackState::render(int, const vector<unique_ptr<Track>>&,
  RenderContext&)`. Renamed rather than silenced with a `using`-declaration
  (a `using` just tells the compiler the collision is intentional; the
  collision itself was still the confusing part) —
  `InstrumentTrackState::render(int frames)` → `renderVoices(int frames)`
  (also renamed in its `ArpeggiatorState` override and all call sites/tests),
  `SongState::render(int, const Song&, Mixer&)` → `renderBlock(...)` (ditto,
  plus ~20 comment references across the codebase that named the old method
  by its qualified name — updated rather than left stale).
- `Song.cpp:529` — explicit `static_cast<int>` on the `uint16_t` row key
  passed to `tinyxml2::XMLElement::SetAttribute`.
- The unused-variable/unused-but-set-variable sites, inspected individually
  rather than blanket-deleted:
  - `HierarchyView.cpp`, `InfoLine.h`, `PatternEditor.cpp` (`renderHeading`'s
    `scene`) — genuinely dead locals (pure getters, no side effects to
    preserve), removed.
  - `Player.cpp` — `pattern_idx`/`row_idx`/`scene` were computed and never
    used at all (not even the structured-binding components); removed the
    whole dead computation, not just the flagged `scene`.
  - `PatternEditor.cpp` (`was_playing`) — only consumer was an `#if 0`
    auto-play-toggle block with no comment, todo, or plan explaining why
    it's disabled or tracking reviving it; removed both together as
    abandoned rather than pending.
  - `main.cpp` (`relative`) — `--relative` has silently done nothing since
    the very first commit that introduced `main.cpp` (confirmed via `git log
    -S`), isn't part of `CLAUDE.md`'s documented flag list, and has no
    connection anywhere in the codebase; removed the flag and its parsing
    entirely rather than leave a CLI option that lies about doing something.
  - `TerminalUI.cpp` (`prev_update`) — declared, never read or reassigned;
    removed.

**2. `-Wsign-compare` (74 sites, medium/mechanical)**
Concentrated in `Note.h` (69), `InstrumentTrackState.h`, `PatternEditor.cpp`,
`InstrumentList.cpp`, `Chart.h`, `Player.cpp`, `InfoLine.h`, `AlsaAudio.cpp`.
Fix each comparison at its actual type mismatch (match the loop/index
variable's declared type to the container's `size_type`, or cast the one
side that's genuinely known non-negative) — no blanket casts, since this is
exactly the warning class that catches real bugs like the `Player.cpp:58`
case above.

**3. `-Wsign-conversion` (the bulk, mechanical, do file-by-file)**
Order by blast radius, largest first: `Track.h` → `Song.h` → `AudioBuffer.h`
→ `Note.h` → `PatternEditor.cpp` → everything else (`SoundFont.cpp`,
`AlsaAudio.cpp`, `UIElement.h`, `PatternBlockOps.cpp`, `Compressor.cpp`, …).
One file (or tightly-related pair) per commit; rebuild and recheck the
warning count after each so the diff stays reviewable and regressions are
caught immediately rather than at the end of a mega-commit. Fixing the first
few headers should shrink the count in every later file that includes them.

**4. Lock it in**
Once stage 3 reaches zero, flip `-Wsign-conversion` (and `-Wsign-compare`,
already implied by `-Wall`) from warning to `-Werror=` in `CMakeLists.txt` —
this is what actually enforces the "new code must compile warning-clean"
rule `CLAUDE.md` already states but nothing currently checks. Also resolve
or delete the dangling `# -Wconversion` comment on line 24: decide whether
to adopt the broader `-Wconversion` (catches float↔int too, much noisier)
as a separate follow-up once sign-conversion is clean — not in scope here.

**5. Two more flags, after a contained cleanup (not yet started)**
- `-Wmissing-field-initializers` (61 sites, concentrated in `TerminalUI.cpp`
  plus one test file) — same bug class as the `ncinput` narrowing fix in
  stage 1: partially-initialized notcurses C structs. Worth a pass before
  enabling as `-Werror=`.
- `-Wuseless-cast` (59 sites, 46 in `SoundFont.cpp`, rest in vendored
  `pocketfft`/now excluded by stage 0) — harmless but mechanical, contained
  to essentially one file.

Checked and rejected for this codebase (noisy without matching signal, not
just "haven't gotten to it yet"): `-Wswitch-enum` (every one of its 29 hits
is a deliberate sentinel enum value — `UNKNOWN`/`UNUSED`/etc. — already
caught by a `default:`; the flag can't distinguish that from a genuinely
forgotten case), `-Wfloat-equal` (756 — DSP code legitimately compares
floats to exact values), `-Wold-style-cast` (848) / `-Wzero-as-null-pointer-
constant` (435) (real modernization value, but each needs its own dedicated
sweep, not a warning-hygiene pass), `-Wunused-parameter` (281 — structural
in an override-heavy codebase), `-Wdouble-promotion` (230 — hits spread
through UI/formatting code, not isolated to the real-time audio path where
it would actually matter).

Run `ctest --test-dir build --output-on-failure` after every stage.
