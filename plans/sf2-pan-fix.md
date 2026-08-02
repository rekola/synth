# Fix SoundFont region pan and stereo sample pairs in voice positioning

## Context

The percussion/pitched-arc key-offset mechanism (`applyPercussionOffset`/`applyPitchedArcOffset`/`applyNormalizedOffset` in `SoundFont.cpp`) resolves a per-key direction from the shared extent model. Separately, `adjustPositionForPan` (older code) folds each SF2 region's own authored pan into azimuth. The two were never unified, and investigation during a live-playback bug report ("percussion sounds left-biased, no drums on the right") surfaced three compounding problems: the two systems double-count the same spatial fact, `adjustPositionForPan`'s pan-to-offset formula has an actual sign/range bug, and multi-region "groups" (stereo-mic pairs baked into the SF2 data) fight the per-key layout when both are active. This plan replaces the SF2 pan path entirely, unifies it with the existing per-key offset pipeline, and adds explicit, measured handling for simultaneous region groups.

## Part 1 findings (measured against `/usr/share/sounds/sf2/FluidR3_GM.sf2`, 189 of 190 presets carry sample data)

**Method, and two pitfalls found and corrected before trusting any numbers:**
1. A naive whole-instrument "pairwise overlap -> connected components" over-chains: adjacent, non-overlapping key zones get transitively merged into implausible 18-98-member "groups" through incidental overlaps elsewhere in the instrument. Fixed by evaluating groups **per MIDI key** (0-127): for each key, gather regions whose key range contains it, then partition *those* candidates by mutual velocity-range overlap. This matches what an actual note-on does and eliminates cross-key chaining.
2. SF2 zones are two-tier (preset-zone generators are merged with instrument-zone generators); a preset with several preset-level velocity-crossfade zones referencing the *same* instrument produced apparent "duplicate" 9-36-member groups in a quick from-scratch parser that only read instrument-zone ranges. **This does not affect the real engine** - `tsf_region.lokey/hikey/lovel/hivel` (`SoundFont.cpp`) are already the correctly-merged, authoritative per-region ranges produced by the existing `tsf_load_presets()` merge, which is exactly why grouping must be built from those fields, not reimplemented ad hoc.

**Corrected results:**
- Group size distribution (per-key, ranges properly merged, exact `(pan, sampleid)` duplicates within a group collapsed): **size 1: 2442, size 2: 6700, size 3: 127, size 4: 69**. No larger true groups exist in this file. Confirms groups of size >2 are real but rare - the algorithm must handle arbitrary size, but size 2 is overwhelmingly dominant.
- Pan value distribution (all effective regions): **-50%: 40.5%, +50%: 39.9%, 0%: 19.6%**, with a negligible long tail (a handful of regions at ±20/25/40%). Pan in this SoundFont is essentially a *discrete three-value* system (hard-left / hard-right / center), not a continuum - directly informs the pan-clustering epsilon below.
- **Piano, Harp, Marimba, Timpani, Alto Sax, Synth Brass**: every key-zone is a hard `-50%`/`+50%` pair of *different* sample IDs (genuine stereo mic capture, re-authored per key zone) - the "differing pan describes width" case, and exactly the double-counting risk described in the prompt: the pair's own width has nothing to do with which key it is, so the arc/table's per-key movement gets a full hard-L/R wash added on top of every note identically.
- **Mixed-group example found**: `Burst Noise` (program 125, bank 9) - one group has a centre-panned *unique* sample plus **three copies of the same sample ID** at pan 0/+50/-50 (a decorrelation trick already baked into the SF2 authoring itself, not something this plan introduces or should imitate elsewhere - see "Explicitly rejected"). These three identical-content copies will show correlation ~1.0 at lag 0, so the collapse rule below sums them safely; the unique centre sample is a separate cluster.
- **Similar-pan/layered-timbre example**: `Metal Pad` (program 93) - three *different* sample IDs, all pan 0, full key range - genuine layered timbre, never spread.
- **Church Organ** (program 19, bank 0) in this specific file: every key is a **single mono, centre-panned region** - no groups at all. The ambience-preserve rule is therefore not exercised by this particular file (nothing to preserve), but is still the correct general classification - a more elaborately multi-mic'd organ SoundFont would exercise it, and leaving Church Organ off the table would incorrectly let it fall into the generic "not a radiator" bucket without documenting *why* pipe organs specifically are excluded from arc treatment.
- **Root-cause confirmation of defect 2, precisely quantified**: `region.pan` is already stored zero-centred in the generator's own `[-0.5, +0.5]` range (`GEN_FLOAT_LIMITPAN`, `* .001f, min -.5f, max .5f`). `adjustPositionForPan`'s formula (`pan_offset = region.pan - 0.5f`, then clamp) double-subtracts: for this file's actual (`-50%/0%/+50%`) data, **hard-left regions accidentally clamp back to the correct value, hard-right regions collapse to dead centre (all rightward panning silently discarded), and centre-panned regions read as hard left**. This fully explains "left bias, nothing on the right" as measured, real behavior, not a guess - independent of and in addition to the unmirrored-pan issue.
- Cross-correlation/lag between actual PCM content is **not yet measured** - it requires reading raw samples via the engine's own `fontSamples_`, which only exists inside `SoundFont.cpp`'s translation unit. This plan implements it as production code (Part 3 needs it at load time anyway) rather than a second throwaway script, and will report actual correlation/lag numbers for a handful of concrete groups (the piano pair, the Burst Noise triple, an Alto Sax pair) once implemented, before finalizing thresholds.

## Design

### Part 2 - one position source

Replace `adjustPositionForPan(position, region)` with a function producing the *same* normalized offset representation the percussion table and arc use: `u = region.pan * 2.0f` (region.pan is already the signed `[-0.5,+0.5]` offset, so this is a straight rescale to the shared `[-1,+1]` convention - no independent width law). Delete the `min(1, 1/distance)` scaling entirely; distance scaling now comes only from `applyNormalizedOffset`'s existing `atan(extent/d)` call.

**Precedence, not summation**: `SoundFontInstrument::playNote()` already knows `is_percussion`/`is_arc` (renamed/extended below to a general "layout active" flag). When layout is active, region pan contributes nothing - `SoundFontVoice`'s existing `skip_native_pan` parameter already exists for exactly this and just needs its scope widened (see Part 3). When layout is inactive, the *now-fixed* pan-based offset becomes the position source, going through `applyNormalizedOffset` like everything else - so the shared mirror flag (`distance <= 1` = player) applies to it automatically, fixing defect 2 for every instrument, not just percussion.

### Part 3 - simultaneous region groups

**Data structures** (`SoundFont.h`, exposed as plain POD so external code doesn't need `tsf_region` internals - also directly reusable for the Part 1 report):
```cpp
struct SF2RegionInfo { int region_index, lokey, hikey, lovel, hivel; float pan, rms; };
struct SF2RegionGroup {
  std::vector<SF2RegionInfo> members;
  std::vector<std::vector<float>> pairwise_correlation;  // members.size() x members.size()
  std::vector<std::vector<int>> pairwise_lag_samples;
};
std::vector<SF2RegionGroup> SoundFont::analyzeRegionGroups(size_t preset_index) const;
```

**Group discovery** (per preset, at `SoundFontInstrument` construction): for each MIDI key 0-127, gather regions whose `[lokey,hikey]` contains it, partition by velocity-range overlap (union-find restricted to that key's own candidates - the per-key evaluation that avoids Part 1's cross-key-chaining pitfall), dedupe identical member-sets across keys. Cheap (regions per preset are typically under a hundred), runs once at load.

**Pan clustering within a group**: sort members by `pan`, merge into a cluster whenever the gap to the next value is `< 0.02` (4% of the full `[-0.5,+0.5]` range - derived from the measured data: real values cluster at exact 0/±0.5 with the smallest genuinely-distinct non-hard value observed at ±0.20, ten times larger, so 0.02 cannot merge two deliberately different pans while absorbing float noise). A single cluster = "layered timbre" (Part 1's similar-pan case) - members always co-render at the same position regardless of layout state, no correlation check needed (they are understood to be genuinely different content, not redundant captures). Multiple clusters = genuine width.

**Correlation/lag** (only computed between cluster *representatives* - each cluster's own highest-RMS member - when a group has 2+ clusters): normalized cross-correlation over the first 4096 samples (~93ms at 44.1kHz - long enough to include the attack transient, short enough to stay cheap and avoid loop-driven artifacts) of each region's PCM window (`fontSamples_[region.offset ..]`), searched over lag `[-N, +N]` where `N` = 1ms of samples (`sampleRate/1000` - the same perceptually-fused-arrival reasoning already used for the floor reflection's Haas-window discussion elsewhere in this codebase, not a new ad hoc constant). "Phase-safe" = peak correlation `>= 0.7` (a standard "majority of variance shared" cutoff: 0.7² ≈ 0.5) **and** `|peak lag| <=` that 1ms bound.

**Per-instrument layout-active decision** (from Part 4's table): `is_percussion` (bank 128) or `is_arc` (family table lookup, extended from today's piano/harp-only check).

**Resolution** (precomputed once per region index into a `vector<RegionPositionRole>`, sized to `regions.size()`, zero-cost per-note-on lookup):
- `layout inactive` (includes the ambience/organ case - see Part 4): every pan-cluster gets its own resolved offset (`u` = cluster's mean `pan * 2`), applied via `applyNormalizedOffset` to every member of that cluster. This is simply Part 2's per-region pan fix applied per cluster instead of per region - no suppression, nothing summed away, the recording's own width is preserved outright.
- `layout active`, single cluster (no real width): all members get the layout-resolved position (existing behavior, unchanged).
- `layout active`, 2+ clusters: if **all** cluster-representative pairs are phase-safe, every member of every cluster gets the layout-resolved position (sum - coherent, level-preserving, since each voice keeps its own region-derived gain, only its *encoded direction* changes). If **any** pair fails, the single highest-RMS region across the whole group is kept (gets the layout position), every other member is marked suppressed (no voice constructed for it at all) - avoiding a comb-filtered mess from forcing incompatible captures to one point.

This produces **at most one direction per note-on for a layout-active instrument**, regardless of group size - the actual verification target for the reported bug.

### Part 4 - instrument span/layout table

Replace `isPitchedArcFamily` with a richer per-family table (bank-0 program ranges; percussion stays its own bank-128 special case, unchanged, since a kit's "program" selects *which kit*, not an instrument family):

```cpp
enum class LayoutShape { None, Linear, Symmetric };
struct InstrumentFamily { float span; LayoutShape layout; float layoutStrength = 1.0f; };
```

| Programs | Family | Span | Layout | Notes |
|---|---|---|---|---|
| 0-7 | Piano | 1.5 m | Linear | `layoutStrength = 1/3` (soundboard radiates as a whole) |
| 9, 13 | Glockenspiel/Xylophone | 0.65 m | Linear | midpoint of stated 0.5-0.8 m range |
| 11, 12 | Vibraphone/Marimba | 1.2 m | Linear | |
| 14 | Tubular Bells | 0.6 m | Linear | |
| 19 | Church Organ | 0 | None | ambience-preserved (see below) |
| 46 | Orchestral Harp | 0.5 m | Linear | unchanged from current behavior |
| 47 | Timpani | 0.8 m | Linear | new |
| everything else | - | 0 | None | not a physically-separated radiator (guitar/winds/voice/synth leads/electronic organs/celesta/music box/dulcimer - not in Part 4's brief, so left at the documented default rather than guessed) |

Selection criterion documented in code: are this instrument's keys physically separated radiators? (Mallets/harp/kit: yes. Piano: weakly, hence the reduced strength. Guitar/winds/voice: no.)

**Ambience exception is not a separate runtime branch.** "Preserve the group, disable the layout" for Church Organ is exactly what `span=0`/`layout=None` already does via the "layout inactive" rule above - the table entry exists purely so the classification is a documented, deliberate choice (with the room-acoustics rationale in a comment) rather than an accidental fallthrough indistinguishable from an unmapped instrument.

`Symmetric` shape is implemented in the data model (pitch mapped to distance-from-centre rather than low-to-high) since it costs little given the shared algebra, but no table row uses it yet - it's there for a future organ-façade case, and the perspective mirror is a documented no-op for it (a useful consistency check).

## Files touched

- `SoundFont.h` - new `SF2RegionInfo`/`SF2RegionGroup` structs, `analyzeRegionGroups()`.
- `SoundFont.cpp` - delete `adjustPositionForPan`; add pan-to-offset conversion, per-key group discovery, pan clustering, correlation/RMS analysis, the `InstrumentFamily` table (replacing `isPitchedArcFamily`), `RegionPositionRole` precomputation, and `playNote()`/`SoundFontVoice` wiring (widen `skip_native_pan`'s scope to "layout active OR pan-fix active" - i.e. always true now, since there is no more competing native-pan path to preserve for anything).
- `tests/SF2ModulatorTests.cpp` - new tests per the verification list below, reusing the existing `writeMinimalSf2`/`PresetSpec` fixture builder (already extended with a `bank` field this session).

## Verification

- Instrument table: one test per layout category (arc-linear, percussion table, span-0, ambience) asserting resolved azimuths match span/shape; span-0 instruments resolve every key to the base direction.
- Ambience: an organ-classified fixture with a synthetic multi-region group preserves multiple encode directions and applies no per-key offset.
- Resolved-azimuth table: log every percussion key's azimuth across {layout on/off} x {player/audience}; assert pan contributes nothing when layout is active, every azimuth flips sign with the mirror, no key resolves to two directions when layout is active.
- Kit symmetry: mean resolved azimuth across a symmetric key set is near the base azimuth in both perspectives (regression test for the reported bug).
- Distance scaling: same instrument at distances 1/3/5 matches `atan(extent/d)` with no saturation at distance <= 1 (replacing the deleted `min(1,1/d)` law).
- Collapse correctness: one encode direction per note-on regardless of group size; rendered RMS within tolerance of the pre-collapse sum.
- Velocity-layer safety: regions overlapping in key but disjoint in velocity are never grouped; a note-on's resolved position is independent of sibling velocity layers.
- Group-size coverage: exercised for a real size-2 group and a real size-3+ group (Burst Noise, measured above); centre-panned members resolve to the key's position.
- Comb safety: summed subsets have measured peak lag under threshold; where exceeded, single-member selection is asserted.
- Full existing suite stays green (`ctest --test-dir build`).

## Constraints honored

- No allocation/locking/IO in the audio callback - all of the above (group discovery, correlation, RMS, table lookup) is precomputed once per `SoundFontInstrument` at construction into a plain `vector<RegionPositionRole>`; `playNote()` only does an array index per matched region.
- No changes to the extent model, shared SH function, bus, or send architecture.
- Diff stays scoped to `SoundFont.{h,cpp}` and its own tests.
