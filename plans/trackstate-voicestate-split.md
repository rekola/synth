# Split `TrackState` into `TrackState` + `VoiceState`

## Context

`TrackState` (`TrackState.h`) is currently one class serving two different
runtime trees:

1. **The track tree** — one persistent `TrackState` per `Track` in
   `song.getTracks()`, built once by `Track::createStateTree()` and walked
   every audio block by `SongState::render()` via
   `render(int frames, const vector<unique_ptr<Track>> & instruments,
   RenderContext & context)`. Root is `SongState` itself; interior nodes are
   `InstrumentTrackState`/`DrumMachineTrackState`/`ArpeggiatorState`, plain
   group nodes (`Group`), or effect wrappers (see below).
2. **The voice chain** — one ephemeral `TrackState` tree per note-on, built
   fresh by `Track::playNote()` from a `song.getInstruments()` definition
   (e.g. `Oscillator` wrapped in `EnvelopeFilter` wrapped in `NoteMultiplier`),
   owned by `InstrumentTrackState::voices_`, walked every block via the
   *other* `render(int frames)` overload.

Because one class answers to both trees, `TrackState` (and its `EffectState`
subclass, and every `effects/*State` beneath it) carries two full `render()`
implementations plus a pile of methods that only make sense for one role:
`track_info_`/`getAllTrackInfo()` (track-tree only), and
`playNote(float,...)`/`stopNote()`/`killNote()`/`fastRelease()`/
`applyAftertouch()`/`applyChannelPressure()`/`adjustAzimuth()`/
`adjustSendMain/A/B()`/`getOwnLoudnessFactor()`/`getLoudness()`/
`getNoteValue()`/`getExclusiveClasses()` (voice-chain only). This is the
mess the user asked to fix.

**Why it's not a clean 1:1 class split.** The obvious move — one hierarchy
for track nodes, a separate one for voice nodes — runs into a real
complication: several concrete `Track` model classes are used in *both*
roles, not just one, and this is actively exercised by real songs, not a
theoretical capability:

- `Group` (`<group>`) and all 8 `effects/` classes (`Amplifier`,
  `BiquadFilter`, `Chorus`, `Compressor`, `Distortion`, `EnvelopeFilter`,
  `ResonantFilter`, `Tremolo`) are parsed by the exact same
  `Song.cpp::parseChildTrack()`/`createTrack()` regardless of whether they
  appear under a song's `<tracks>` or inside an `<instruments>` definition.
- Confirmed against the actual `songs/*.xml` fixtures: `demo11.xml` has
  `<resonantFilter><distortion><track .../></distortion></resonantFilter>`
  directly inside `<tracks>` — a persistent, always-rendered wrapper around
  an `InstrumentTrack`. The same `resonantFilter`/`distortion`/`envelope`/
  `multiply`/`amplifier`/`compressor`/`biquadFilter` elements also appear,
  in nearly every song, inside `<instruments>` wrapping `Oscillator`/
  `SoundFontInstrument` — the ephemeral per-note role.
- `Group` appears only inside `<instruments>` in the current song set (a
  voice-side fan-out group), but nothing in the code restricts it to that
  role — `Track::createState()`'s default plain-`TrackState` return is
  exactly as capable of being a track-tree folder.
- Everything else is single-role and already only ever reached one way:
  `InstrumentTrack`/`PercussionTrack`/`DrumMachineTrack`/`Arpeggiator`
  override `createState()` themselves and never appear inside
  `<instruments>`; `Oscillator`/`Noise`/`LFO`/`FileInstrument`/
  `NoteMultiplier`/`SoundFontInstrument`/`GenericInstrument` override
  `playNote()` directly (building their own leaf/group voice state without
  ever calling `createState()`) and never appear inside `<tracks>`.
  Confirmed by grepping every `songs/*.xml` fixture for cross-contamination
  — none found.

So `StatefulSongObject::createState()` is called from exactly two places —
`Track::createStateTree()` (track role) and the default body of
`Track::playNote()` (voice-group role) — and the only classes that rely on
*both* call sites are `Group` and the 8 `Effect` subclasses. That's the
actual shape of the "dual-role" problem: 9 classes, not the whole
hierarchy. A class split has to keep those 9 usable in both trees without
regressing today's behavior (wrapping either a persistent track or an
ephemeral voice, e.g. distortion audibly applied per-track for `demo11.xml`
and per-note-instrument-definition for `epiano_test1.xml` alike).

## Design

**New shared base, `TreeNode`** (name tentative — could also just be an
unexported implementation detail if templates make it unnecessary): holds
exactly what both trees genuinely share — `channel_config_`, `children_`,
`addChild()`/`getChildren()`/`getChildByInternalId()`/`removeChild()`, and
the static `gainToDecibels()`/`decibelsToGain()`/`getRandF()` helpers.
`addChild()` is used generically by both trees today (`Track.h`'s
`createStateTree()`/`playNote()` for the track side; `GenericInstrument.h`/
`Oscillator.cpp`'s LFO-modulator attach, `NoteMultiplier.cpp`, and
`SoundFont.cpp`'s multi-region group build for the voice side) — this stays
true after the split. `getChildByInternalId()`/`removeChild()` are only
ever called on the track tree in practice (`Track.h`, `Player.cpp`) but
cost nothing to keep on the shared base.

**`TrackState : public TreeNode`** keeps only the track-tree surface:
`render(int frames, const vector<unique_ptr<Track>>&, RenderContext&)`,
the `renderChildren(frames, instruments, context, accumulator_config)`
helper, `isActive()`, `clear()`, `track_info_`/`getTrackInfo()`/
`setTrackInfo()`, `getAllTrackInfo()`. Drops the voice-shaped `render(int
frames)` overload and its `renderChildren(frames, accumulator_config)`
helper entirely — nothing outside the voice chain ever calls them on a
track-tree node (`SongState::render()` doesn't even go through
`TrackState::render()` at all; it has its own bespoke top-level loop).

**New `VoiceState : public TreeNode`** takes the rest: `render(int
frames)` + its own `renderChildren(frames, accumulator_config)`,
`playNote(float,float,int)`, `stopNote()`, `killNote()`, `fastRelease()`,
`applyAftertouch()`/`getAftertouch()`, `applyChannelPressure()`/
`getChannelPressure()`, `adjustAzimuth()`, `adjustSendMain/A/B()`,
`getOwnLoudnessFactor()`/`getLoudness()`, `getNoteValue()`,
`getExclusiveClasses()`, `isActive()` (voice-shaped: "still sounding"),
`getVoiceCount()`/`getAllocatedVoiceCount()`. No `track_info_`, no
`getAllTrackInfo()`/`getAllActiveVoices()` — those stay exclusively on
`TrackState`, and `InstrumentTrackState::getAllActiveVoices()` already
reads directly from its own `voices_` rather than delegating polymorphically,
so nothing there needs to change shape.

**Retype every voice-holding site** from `unique_ptr<TrackState>`/
`vector<unique_ptr<TrackState>>` to `VoiceState`:
`InstrumentTrackState::voices_`, `Track::playNote()`'s return type (and
every override: `Oscillator`, `Noise`, `LFO`, `FileInstrument`,
`NoteMultiplier`, `SoundFontInstrument`/`SoundFont.cpp`,
`GenericInstrument`), `InstrumentVoice`, `SoundFontVoice`. This is
mechanical — none of these classes need any logic changes, just a base
class and a few signature updates.

**Split `EffectState` into `EffectTrackState`/`EffectVoiceState`.** Each
currently has one `applyEffect(AudioBuffer&)` override plus, for the two
nonlinear/mono-reducing effects (`Chorus`, `Distortion`), a shared
`reencodeIfNeeded()`. Factor each effect's actual DSP fields + logic
(`applyEffect()`, `reencodeIfNeeded()` where present) into a small
non-polymorphic helper the two role-specific classes both own, e.g.:

```cpp
// Distortion.cpp
class DistortionDsp {
public:
  DistortionDsp(DistortionType type, float param, float drymix, float drive) : ... { }
  void applyEffect(AudioBuffer & input) const { ... }        // unchanged body
  AudioBuffer reencodeIfNeeded(const ChannelConfiguration &, AudioBuffer) const { ... } // unchanged body
private:
  DistortionType type_; float param_, drymix_, drive_;
};

class DistortionTrackState : public EffectTrackState {
  DistortionDsp dsp_;
public:
  DistortionTrackState(const ChannelConfiguration & cfg, DistortionType t, float p, float d, float dr)
    : EffectTrackState(cfg), dsp_(t, p, d, dr) { }
  AudioBuffer render(int frames, const vector<unique_ptr<Track>> & instruments, RenderContext & context) override {
    auto reduced = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, instruments, context, reduced);
    dsp_.applyEffect(data);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), std::move(data));
  }
};

class DistortionVoiceState : public EffectVoiceState {
  DistortionDsp dsp_;
public:
  // ... same constructor shape ...
  AudioBuffer render(int frames) override {
    auto reduced = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, reduced);
    dsp_.applyEffect(data);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), std::move(data));
  }
};
```

`Track::createState()` (the single `= 0` factory in
`StatefulSongObject`) becomes two factories:

```cpp
virtual std::unique_ptr<TrackState> createTrackState(const ChannelConfiguration &) const { assert(0); return nullptr; }
virtual std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration &) const { assert(0); return nullptr; }
```

with sensible (non-asserting) defaults only where a base class already
provides one today (`Track`'s own default — used by `Group` — returns a
plain, non-abstract node from both). `Track::createStateTree()` calls
`createTrackState()`; the default body of `Track::playNote()` calls
`createVoiceState()`. Every currently-single-role class overrides exactly
one of the two (`InstrumentTrack`/`DrumMachineTrack`/`Arpeggiator`
override only `createTrackState()`; they don't need `createVoiceState()`
since none of them are ever reached via `playNote()`'s default body —
their `playNote()` semantics don't apply here since they're never nested
inside an instrument definition). `Group` and each of the 8 `Effect`
subclasses override both, one line apiece delegating to the two thin
state constructors.

**`Effect::getChildChannelConfiguration()`/`reduceForEffect()`** (used by
`Distortion`/`Compressor` overrides) don't change — they're plain
`ChannelConfiguration` transforms, unrelated to the state-class split, and
already called identically from both new `render()` overloads.

## Scope / files touched

- `TrackState.h` — split into `TreeNode` + slimmed `TrackState`.
- New `VoiceState.h`.
- `effects/EffectState.h` — split into `EffectTrackState`/`EffectVoiceState`
  (both in the same header, or two headers — minor).
- `effects/{Amplifier,BiquadFilter,Chorus,Compressor,Distortion,
  EnvelopeFilter,ResonantFilter,Tremolo}.{h,cpp}` — each gains a DSP helper
  + two thin state classes in place of one `*State`; each header's
  `createState()` declaration becomes `createTrackState()`/
  `createVoiceState()`.
- `Group.h` — `createState()` → two one-line overrides.
- `Track.h`/`StatefulSongObject.h` — factory split described above;
  `playNote()`'s default body's `createState(config)` call →
  `createVoiceState(config)`; `createStateTree()`'s call →
  `createTrackState(config)`.
- `InstrumentTrack.h/.cpp`, `DrumMachineTrack.h/.cpp`, `Arpeggiator.h/.cpp`,
  `PercussionTrack.h` — rename their `createState()` override to
  `createTrackState()` (no logic change).
- `InstrumentVoice.h`, `SoundFont.cpp` (`SoundFontVoice` + the multi-region
  group builder + LFO-modulator attach), `Oscillator.{h,cpp}`,
  `Noise.{h,cpp}`, `LFO.{h,cpp}`, `FileInstrument.{h,cpp}`,
  `NoteMultiplier.{h,cpp}`, `GenericInstrument.h` — retype
  `unique_ptr<TrackState>` returns/params to `VoiceState` (mechanical).
- `InstrumentTrackState.h` — `voices_` becomes
  `unordered_map<int, vector<unique_ptr<VoiceState>>>`; every method
  signature taking/returning a voice (`addVoice`, `retriggerVoices`,
  `chokeExclusiveClasses`'s `const TrackState & new_voice` param, etc.)
  retypes to `VoiceState`. No behavior change.
- `DrumMachineTrackState.h`, `ArpeggiatorState.h/.cpp` — no logic change,
  just inherit the retyped `InstrumentTrackState`.
- `SongState.h` — no logic change (never used the voice-shaped `render()`
  or any voice-only method to begin with).
- `Player.cpp` — `dynamic_cast<InstrumentTrackState*>` calls unaffected;
  `getAllTrackInfo()`/`getAllActiveVoices()` calls unaffected (both stay on
  `TrackState`).
- Tests: `tests/AzimuthSlideTests.cpp`, `tests/SendLiveUpdateTests.cpp`,
  `tests/SF2ModulatorTests.cpp`, `tests/VoiceLoudnessTests.cpp`,
  `tests/EnvelopeFilterTests.cpp` construct/inspect voice chains directly
  and need their declared types updated from `TrackState`/
  `unique_ptr<TrackState>` to `VoiceState`; `tests/DrumMachineTrackTests.cpp`/
  `tests/ArpeggiatorStateTests.cpp`/`tests/RenderTests.cpp` go through
  `Track`/`Song` and shouldn't need changes beyond compiling.

## Suggested phasing (each phase keeps `ctest` green)

1. Extract `TreeNode` under `TrackState` unchanged otherwise — pure
   mechanical, zero behavior change, smallest possible first diff to
   validate the seam.
2. Introduce `VoiceState` as a straight copy of today's voice-only surface,
   still deriving `InstrumentVoice` from it but leaving `TrackState` as-is
   (both classes temporarily overlap) — get one leaf type over the line
   end-to-end before touching the effects.
3. Migrate the remaining single-role voice classes (`SoundFontVoice`,
   `Oscillator`/`Noise`/`LFO`/`FileInstrument` playNote returns,
   `NoteMultiplier`, `GenericInstrument`) and `InstrumentTrackState::voices_`'s
   element type together (they're mutually dependent — voices_ must hold
   the same type every leaf now returns).
4. Split `EffectState`/`Effect::createState()` and all 8 concrete effects —
   the highest-risk phase since it's the only one genuinely duplicating
   code (the thin state wrappers); do one effect first (`Distortion`,
   already has the `reencodeIfNeeded()` precedent above) to nail the
   pattern, then the remaining 7 mechanically.
5. `Group` last (trivial once the pattern from phase 4 exists) — then
   delete the now-dead voice-only members from `TrackState` and the
   now-dead track-only members from `VoiceState` if any were left as a
   transitional straddle in phases 1-2.
6. Remove `TrackState`'s now-unused voice-shaped `render(int frames)`/
   `renderChildren(frames, accumulator_config)` once nothing calls them
   through that type any more.

## Open questions

- Naming: `TreeNode` vs. something more domain-specific
  (`PlaybackTreeNode`?) — open to whatever reads best once it exists.
- Whether `EffectTrackState`/`EffectVoiceState` belong in one header or
  two; the 8 effects' own DSP-helper-plus-two-wrappers pattern likely
  wants a small doc comment once (in `EffectState.h` or a new
  `effects/EffectState.md`-style comment block) explaining *why* every
  effect duplicates this shape, pointing back at this plan, so a future
  reader doesn't mistake it for copy-paste drift.
