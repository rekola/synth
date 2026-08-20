# Instrument identity, generator overrides, instrument maps

## Status of the taxonomy doc and real SoundFonts

The taxonomy doc already exists in the tree, uncommitted, as
`docs/instrument-paths.md` (the planning prompt calls it
`gm-instrument-path-taxonomy.md`; that name doesn't exist anywhere in the repo or
its history - I'm treating `docs/instrument-paths.md` as the real thing and using
its actual name throughout this plan). It already contains the full bank-0 table,
the bank-128 kit list, the root/default tables, and a "known-arbitrary
placements" section - i.e. it reads like a finished v1, not a draft.

**Item 1 is now done.** All three fonts turned out to be installed at
`/usr/share/sounds/sf2/{FluidR3_GM,TimGM6mb,default-GM}.sf2` (not under `data/`,
which is the project-local override path and stays empty/gitignored - these are
the system-wide fallback locations `findDefaultSoundFont()` already searches).
I wrote `tools/verify_sf2_taxonomy.py` (parses `phdr` directly, no dependency on
`SoundFont.cpp`'s full loader) and ran it against all three. Results:

- **Bank 0 is fully populated, 0-127, contiguous, in all three fonts** - the
  `index == program number` assumption every current `sf->createInstrument(N,
  ...)` call relies on holds today, everywhere. Every "MISSING"/"NAME MISMATCH"
  in the raw tool output below is a font using its own creative preset name
  (`"Yamaha Grand Piano"` for GM program 0, `"E.Piano 1"` for program 4, etc.)
  where the doc's `GM name` column - documenting the *official* GM name, not any
  particular font's own label - naturally differs. None of those are taxonomy
  errors; I'm not listing all 45-53 of them here, they're in the tool's stdout
  if you want the raw diff.
- **One real doc discrepancy, now fixed**: `128:56` (`kit.sfx`, "SFX" kit) doesn't
  exist in *any* of the three fonts. The doc already flagged this row as
  unverified ("it has not been checked against that file's `phdr` chunk"); now
  checked, and wrong - fixed in `docs/instrument-paths.md` itself (kept the row,
  since `kit.sfx` is real GS/GM2 numbering, just corrected the surrounding text
  to state plainly that it isn't reliably implemented, described generally
  rather than naming the three specific files checked - this doc is a durable
  reference for arbitrary future SoundFonts, not a record of this session's
  verification). `docs/instrument-paths.md` is folded into Phase 0's own file
  list below as a result, rather than landing separately later.
- **A live, currently-broken bug, found as a side effect**: `InstrumentProvider.h`'s
  `addInstrument(sf->createInstrument(160, "Percussion"))` doesn't do what it
  looks like it does on any of the three fonts. `160` is a raw index into the
  *global* sorted-by-`(bank,program)` preset array, not a bank-128 program
  number - and `FluidR3_GM.sf2`/`default-GM.sf2` both carry presets in banks 8,
  9, and 16 (extra articulation/variant banks) sorted *before* bank 128, which
  pushes every bank-128 index off by 30. Index 160 actually lands on `(128, 2,
  "Standard 2")` - a kit *variant*, not the standard kit at `(128, 0)`. On
  `TimGM6mb.sf2` (only 136 presets total, no banks 8/9/16) index 160 is out of
  range entirely - `createInstrument(160, ...)` still constructs a
  `SoundFontInstrument`, `getElementName`/`getName` and all, but every
  boundary check inside it (`SoundFontInstrument::playNote()`'s
  `preset_ <= f->presets_.size()`, `getDefaultExtent()`'s mirror check) fails,
  so it's a legitimate-looking, silently mute "Percussion" instrument. **23
  song files currently reference `name="Percussion"`** (`grep -rl
  'name="Percussion"' songs/`), so this isn't hypothetical - see "The
  `Percussion` alias" under Phase 0 below for what I'm proposing to do about it
  now vs. leave for Phase 3.

## What's already true about this codebase (load-bearing for the plan below)

A few things aren't obvious from the prompt and change the design:

- **Instruments already live in a shared, song-level pool**, not inline per
  track. `Song::instruments_` (`Song.h:262`) is a top-level
  `vector<unique_ptr<Track>>`, serialized as the song's own `<instruments>`
  element (sibling of `<tracks>`), parsed/written with the exact same
  `parseChildTrack`/`storeChildTrack` recursion used for the track tree
  (`Song.cpp:375-383`, `:501-502`). An `InstrumentTrack`'s `instrument="N"`
  XML attribute (`InstrumentTrack.cpp:39,57`) is a **positional index into
  that vector**, not a reference to anything's stable id. So Phase 1's first
  open question ("where instruments are defined - in place or in a shared
  pool") is already answered by the existing architecture: shared pool,
  referenced by index. Nothing in Phases 0-2 needs to change this; the
  deferred index-to-id rename described in the prompt is exactly the follow-up
  this implies, and stays deferred.
- **The resolved backend instance is shared across every pool slot that
  requests the same name.** `GenericInstrument::prepare()` does
  `concrete_instrument_ = provider.getInstrumentByName(getName())`
  (`GenericInstrument.h:45`), and `getInstrumentByName` returns the *same*
  `shared_ptr<Instrument>` to every caller that asks for the same string
  (`InstrumentProvider.h:82-89`). Two `<instrument name="piano.acoustic.grand">`
  pool slots in one song end up pointing at the *same* `SoundFontInstrument`
  C++ object. This is why Phase 2's generator overrides cannot be stored on
  the resolved backend - they'd leak across every slot sharing that name. They
  have to be stored on the pool-slot node (`GenericInstrument` itself, which
  *is* per-slot) and threaded down through the existing `playNote()` call
  chain as a parameter, the same way `detune`/`sends`/`note_coord` already are.
- **`SoundFontVoice::voiceRegion_` is a raw pointer into load-time-owned,
  shared data** (`&f->presets_[preset].regions[region_idx]`,
  `SoundFont.cpp:940`), where `f` is reached via a `shared_ptr<SoundFontFile>`
  owned by the one `SoundFont` object a whole `InstrumentProvider` constructs
  per loaded file. Every voice from every song, from every instance of every
  preset, reads through the same region struct. **This struct must not be
  mutated for an override** - confirms the override has to be applied as a
  local substitution at each of `voiceRegion_->initialFilterFc`'s two use
  sites, never by writing through the pointer.
- There are exactly two use sites of `initialFilterFc` in the whole voice
  lifecycle: the initial lowpass setup (`SoundFont.cpp:984,988`) and the
  per-block dynamic recompute that only runs when LFO/env modulation is
  targeting the filter (`SoundFont.cpp:1339`, feeding the `dynamicLowpass`
  path). Both need to see the override.
- `SoundFont::createInstrument(size_t preset, ...)` takes a raw index into
  `SoundFontFile::presets_` (the position after `tsf_load_presets()`'s
  `(bank, program)` sort - `SoundFont.cpp:608-633`). `SoundFontFile` already
  has a working `getPresetIndex(int bank, int preset_number)`
  (`SoundFont.cpp:472-479`) - it's just not reachable from outside
  `SoundFont.cpp`, since `SoundFontFile` is only forward-declared in
  `SoundFont.h` and `SoundFont::sf_` is private. Confirms the prompt's
  proposed fix (`SoundFont::createInstrumentByProgram`) is exactly the missing
  piece, not a rewrite.
- `InstrumentTrack`'s own element name is `"track"`
  (`InstrumentTrack.h:14`), and the string `"instrument"` is used today only
  as an XML *attribute* name (the pool-index reference above), never as an
  element name. `<instrument>` is free for Phase 0 to claim; there's no
  conflict to report.
- Percussion abbreviations (`"BD"`, `"SD"`, ...) currently live in
  `src/model/Note.h:119-197`, not in `PercussionTrack` itself - close to what
  the prompt says ("hardcoded in the track") but worth correcting for Phase 3.
- **Old songs' `name=` values won't survive Phase 1's alias-table deletion as
  originally scoped** - moved into Phase 0 at your request; see below.

## Decisions locked in (2026-08-20)

- **`"Electric Piano"`/`"Viola"` quirks**: faithful rename in both cases -
  `piano.electric.grand`/`string.bowed.violin`, programs unchanged. No audio
  change.
- **`Percussion` alias**: pull the index fix forward. `SoundFont::
  createInstrumentByProgram` (originally scoped as Phase 1's "SF2 lookup fix")
  gets implemented now, in Phase 0, specifically so
  `InstrumentProvider.h` can call `createInstrumentByProgram(128, 0,
  "Percussion")` instead of the broken `createInstrument(160, "Percussion")`.
  Real behavior change to 23 songs (their percussion starts sounding like the
  standard kit instead of a kit variant or silence) - confirmed by ear, not
  just by byte-diff, for those songs specifically. **Done and confirmed
  working.**
- **Native (non-curated) preset names**: folded into Phase 0 too, checked
  against the real `FluidR3_GM.sf2` now available. Every `<instrument
  name="...">` matching one of that font's native preset names (and not
  already covered by the curated-alias table) gets rewritten to
  `native:<name>`, and `InstrumentProvider::loadSoundFont()`'s `createAll()`
  fallback registers under that same `native:`-prefixed key from the start
  (Phase 0, not deferred to Phase 1) - so no song ever passes through a
  broken intermediate state where a native name briefly resolves to nothing.

## Phase 0 - element rename, plus alias-to-path rename

### Scope

Two mechanical rewrites over the same file set, done together since both touch
every song's `<instrument>` elements and both get the identical
render-diff verification:

1. `<genericInstrument>` -> `<instrument>` (the element rename).
2. Every curated alias `name="..."` value that `InstrumentProvider.h`'s
   `loadSoundFont()` currently registers -> its taxonomy path, in both the song
   files *and* the registration code itself - i.e. `name="Rhodes Piano"`
   becomes `name="piano.electric.tine"` in the XML, and
   `addInstrument(sf->createInstrument(4, "Rhodes Piano"))` becomes
   `addInstrument(sf->createInstrument(4, "piano.electric.tine"))` in
   `InstrumentProvider.h`. Both sides change in the same commit, so a song's
   name and the string the provider registers stay the same value throughout -
   this is still a plain string-lookup rename, not the walk-up resolver (that's
   still Phase 1); it just happens to rename to strings that are *already*
   full, exact taxonomy paths, so no walk-up is needed for these ~25 to keep
   resolving.

Doing the rename this way (rather than waiting for Phase 1's `registerPath`/
`resolvePath` machinery) means: no functional/audio change at all - same
`sf->createInstrument(programNumber, ...)` call, same underlying preset,
different string key on both ends - and it can reuse Phase 0's render-diff
verification directly, since "the label changed but nothing about what plays
changed" is exactly what that check proves.

### The rename table

Built by cross-referencing `InstrumentProvider.h:27-68`'s current calls against
`docs/instrument-paths.md`'s bank-0 table (now verified against real
`FluidR3_GM.sf2`/`TimGM6mb.sf2`/`default-GM.sf2` - see "Status," above; bank 0
is `index == program` in all three, so program numbers below are also safe
`createInstrument()` indices as-is):

| Program | Current alias(es) | New path |
|---|---|---|
| 0 | `"Piano"`, `"Acoustic Grand Piano"` | `piano.acoustic.grand` (collapse to one registration) |
| 1 | `"Bright Acoustic Piano"` | `piano.acoustic.grand.bright` |
| 2 | `"Electric Grand Piano"`, `"Electric Piano"` | `piano.electric.grand` (collapse - see quirk 1 below) |
| 3 | `"Honky-tonk Piano"` | `piano.acoustic.upright.honkyTonk` |
| 4 | `"Rhodes Piano"` | `piano.electric.tine` |
| 5 | `"Chorused Piano"` | `piano.electric.fm` |
| 6 | `"Harpsichord"` | `keyboard.plucked.harpsichord` |
| 7 | `"Clavinet"` | `keyboard.electric.clavinet` |
| 24 | `"Acoustic Guitar (nylon)"`, `"Acoustic Guitar"` | `guitar.acoustic.nylon` (collapse) |
| 25 | `"Acoustic Guitar (steel)"` | `guitar.acoustic.steel` |
| 34 | `"Electric Bass (finger)"` | `bass.electric.finger` |
| 40 | `"Violin"`, `"Viola"` | `string.bowed.violin` (collapse - see quirk 2 below) |
| 42 | `"Cello"` | `string.bowed.cello` |
| 45 | `"Pizzicato Strings"` | `string.bowed.ensemble.pizzicato` |
| 46 | `"Orchestral Harp"` | `string.plucked.harp` |
| 62 | `"Synth Brass 1"` | `brass.synth` |
| 63 | `"Synth Brass 2"` | `brass.synth.soft` |
| 87 | `"Lead 8"` | `lead.bassLead` |
| 88 | `"Pad 1 (new age)"` | `pad.newAge` |
| 89 | `"Pad 2 (warm)"` | `pad.warm` |
| 90 | `"Pad 3 (polysynth)"` | `pad.poly` |
| 91 | `"Pad 4 (choir)"` | `pad.choir` |
| 92 | `"Pad 5 (bowed)"` | `pad.bowed` |
| 93 | `"Pad 6 (metallic)"` | `pad.metallic` |
| 94 | `"Pad 7 (halo)"` | `pad.halo` |
| 95 | `"Pad 8 (sweep)"` | `pad.sweep` |

`sf->createInstrument(160, "Percussion")` is deliberately **not** in this
table - see "The `Percussion` alias," below, it needs a decision, not a rename.

Two pre-existing quirks this rename makes visible rather than introduces -
flagging both for your call before I touch anything, since "rename only" means
I should not silently repoint either of these to a different program:

1. **`"Electric Piano"` is bound to program 2 (Electric Grand Piano), not
   program 4/5** (the actual Rhodes/FM electric pianos). Anyone who authored
   `name="Electric Piano"` expecting a Rhodes-ish tone has actually always been
   getting the electric *grand* patch. A faithful rename collapses it into the
   program-2 row above (`piano.electric.grand`) alongside `"Electric Grand
   Piano"`, since that's genuinely the same registration today. If this was
   never intentional, it's a one-line fix to point `"Electric Piano"` at
   program 4 instead before renaming - your call, not mine to silently decide.
2. **`"Viola"` is bound to program 40 (Violin), not program 41 (the real
   Viola).** Same shape: a faithful rename collapses `"Viola"` into
   `string.bowed.violin` alongside `"Violin"` itself, honestly reflecting that
   both aliases have always played the same violin patch. Whether that's an
   intentional GM-substitute (some minimal fonts skip a distinct viola sample)
   or a copy-paste slip in the original alias table, I can't tell from history
   alone - flagging rather than guessing.

Songs using any of the above aliases get their `name=` value rewritten to match
whichever row they fall under (so `name="Electric Piano"` and `name="Electric
Grand Piano"` both become `name="piano.electric.grand"` if quirk 1 is kept
as-is).

### The `Percussion` alias - needs a decision, not a rename

Found while building the table above, and independent of it: **23 song files
reference `name="Percussion"`**, and on every font checked here it is
currently either silently wrong (`FluidR3_GM.sf2`/`default-GM.sf2`: plays kit
variant `128:2` "Standard 2" instead of the standard kit `128:0`) or silently
mute (`TimGM6mb.sf2`: index out of range, constructs a real but permanently
non-sounding instrument). This isn't something a pure rename can fix - the
correct target path (`kit.standard`) only exists once `<instrumentMap>`
resolution is real, which is Phase 3, and repointing the raw index from `160`
to whatever gets the right *preset* is a behavior change (making silent/wrong
songs start making percussion sound), not a label change.

Options, not picking one for you:
- **Leave it exactly as-is in Phase 0** (buggy index, alias name unchanged,
  excluded from this rename pass) and fix it properly in Phase 3 alongside real
  `<instrumentMap>` support. Zero risk added by this phase; the bug keeps
  existing exactly as it does today.
- **Pull forward just the index fix** once Phase 1's `createInstrumentByProgram(128,
  0, "Percussion")` exists (small, and now proven necessary - not merely
  theoretical) - fixes the wrong-kit-variant/silence bug immediately without
  waiting for full Phase 3 kit-map machinery, keeping the plain string name
  `"Percussion"` (not a taxonomy path, since `kit.` paths aren't real yet).
  This is a real behavior change to 23 existing songs (their percussion track
  would start sounding different - correct - instead of wrong or silent), so
  it'd need the same render-diff-style verification treated as an *expected*
  diff, confirmed by ear, rather than a "must be identical" check.

I'd lean toward the second option, given how many songs are affected and that
it's currently either wrong or silent on the fonts we actually have, but I'm
not doing it without you picking.

### Element rename

Rename `<genericInstrument>` to `<instrument>`:
- `GenericInstrument::getElementName()` (`GenericInstrument.h:34`)
- `Song.cpp:119`'s `createTrack()` dispatch (`if (name == "genericInstrument")`)
- The `SoundFont.cpp:1086` comment showing an example fragment (update the
  example, not because it's parsed, but because `no-historical-narration`
  conventions mean comments should show current syntax)
- Every song file's `<genericInstrument` tags, open and close, everywhere they
  appear (they appear both inside `<instruments>` pool entries and, per the
  `SoundFont.cpp` comment, potentially nested under a filter/effect chain)

### Song rewrite mechanics

53 files reference `genericInstrument` today: `songs/*.xml` (all of them but a
handful - `bass_test2.xml`, `epiano_test1.v0.xml`, and `songs/backup/*` included,
since backup copies are still real files that would silently stop loading
otherwise) plus one test fixture, `tests/fixtures/sf2_reverb_send.xml`. Two
already-uncommitted new song files in the working tree
(`songs/arptest2.xml`, and the pending edits to `songs/arptest1.xml`/
`songs/songtest19.xml` visible in `git status`) need the same treatment, so this
has to run *after* whatever the user is currently doing to those files, not
before - worth confirming before touching them (see the "separate song
commits" convention: my edits to `songs/*.xml` for this rename still shouldn't
be folded into the same commit as the user's own in-flight edits to those
files).

Mechanism: a plain text substitution (`sed -i` or equivalent) of `<genericInstrument`
-> `<instrument` and `</genericInstrument>` -> `</instrument>` across every
`songs/*.xml`, `songs/backup/*.xml`, and `tests/fixtures/*.xml` file, plus the
two source-code occurrences by hand. Self-closed tags
(`<genericInstrument name="..."/>`) are covered by the open-tag substitution
alone. No XML-aware tool is needed for a pure element-name swap; a blind text
substitution is actually *safer* here than parsing and re-serializing every
file, because re-serialization through tinyxml2 would reformat whitespace/
attribute order and produce a diff that's nearly impossible to review against
"did this actually only rename the element."

### Verification

Covers both rewrites at once (element rename + alias-to-path rename), since
both are supposed to be 100% audio-silent changes and both get judged by the
same check. Now fully runnable in this environment - `FluidR3_GM.sf2` is
installed at `/usr/share/sounds/sf2/`, so `--render` isn't blocked the way I
originally expected when I wrote the "Status" section above:

1. Before the rewrite: for every `songs/*.xml`, run `./build/synth --render
   /tmp/before/<name>.wav songs/<name>.xml` (headless, no terminal/audio device
   needed per `CLAUDE.md`).
2. Apply both rewrites.
3. After: `./build/synth --render /tmp/after/<name>.wav songs/<name>.xml`.
4. Byte-compare (or sample-compare with a zero tolerance, since nothing about
   either rewrite should perturb even dither) every `before`/`after` pair. Any
   difference means the rewrite changed something audible, which means either
   the rewrite is wrong (a mis-renamed pair unbalancing the tree, an alias
   collapsed into the wrong path, a file where `genericInstrument` also
   appeared somewhere unexpected) or a file failed to load at all after the
   change (silent fallback to `default_instrument` would itself produce a
   different render, so this check also catches silent load failures without
   extra plumbing) - **except** for songs referencing `name="Percussion"`,
   which are expected to render identically only if the `Percussion` alias is
   left untouched (see above); if you pick the "pull the index fix forward"
   option, those songs are expected to differ, confirmed by ear instead.
5. Any song that still fails to render for unrelated reasons gets the rewrite
   applied anyway and a basic `Song::open()` sanity check (via a tiny test/
   tool, not the full UI) that it still parses.

This is stronger than "open a few songs and confirm they load unchanged" (the
user's own planned manual check) - it covers all ~50 files mechanically and
catches silent failures a listen test could miss.

**Found while actually running this**: raw byte-diff alone is not sufficient
here, and gave a misleading first result (67/67 songs differed) that needed a
second pass to explain. Root cause: `SongObject::internal_id_`
(`SongObject.h`) is assigned from one process-global `std::atomic<int>
next_id`, shared by *every* `SongObject`-derived instance ever constructed -
tracks, patterns, instruments, everything. `InstrumentProvider`'s constructor
+ `loadSoundFont()` run once at startup, before any song loads, and construct
one object per `sf->createInstrument(...)` call. Collapsing the 4 duplicate
alias pairs (see the rename table above) means 4 fewer objects get
constructed there than before - which shifts every `internal_id_` assigned to
*every later-constructed object in the whole process* down by 4, including
every track in every subsequently-loaded song. `NoteCoordinate::track_id_`
(`NoteCoordinate.h`) is built from a track's `getInternalId()`
(`SongState.h:145`'s `getPatternsByTrack()` keys), and feeds
`HashField`-derived per-voice jitter/phase/decorrelation seeding
(`InstrumentVoice.h`'s `kNotePhaseSalt`, percussion offset jitter, unison
decorrelation) - so the id shift silently perturbs those seeds for literally
every voice in every song, even ones with zero SoundFont content (confirmed
directly: `songtest18.xml`, built entirely from `<oscillator>` elements with
no `<instrument>`/SoundFont reference anywhere in it, still rendered
differently). None of this is a bug in the rename - it's a real, pre-existing
property of the codebase: these "deterministic" hash seeds are only
deterministic *for a fixed binary*, not stable across any code change that
alters how many `SongObject`s get constructed before a song loads, which
`InstrumentProvider`'s startup sequence does on every build. Isolated by a
one-time diagnostic (not shipped): padding the rewritten alias block back
out to the original 31 `addInstrument()` calls (4 throwaway duplicate
registrations, discarded after the test) made every one of the 67 songs
byte-identical to baseline again, **except** the 11 that reference
`"Percussion"` - exactly and only the songs affected by the intentional,
approved index-160 fix, confirming the rename itself is 100% audio-neutral
and the only real change is the one that was meant to happen. Worth knowing
for any future "should be a no-op" refactor in this codebase: byte-diffing
renders is only a valid check once this coupling is accounted for (padding
the diagnostic back to a matching object count, or comparing gross/statistical
properties instead of raw bytes) - naive byte-diffing will show every song in
the corpus as "changed" for almost any code change that touches how many
objects get constructed before the first song loads, real audio change or
not.

### On the deferred attribute rename

Explicitly not doing it now, per the prompt. Noting for the record since Phase
0 is the cheapest place for it: renaming `instrument="N"` to something that
doesn't presuppose "points at an instrument," and switching it from a
positional index to a text-id reference, both touch every song file's
`<track>`/`<arpeggiatorTrack>`/`<drumMachineTrack>` elements the same
mechanical way this phase's element rename does. Flagging again at
implementation time rather than doing it unasked.

## Phase 1 - instrument identity

### `docs/instrument-paths.md` verification (item 1)

Done - see "Status," above. `tools/verify_sf2_taxonomy.py` exists, was run
against all three installed fonts, one real doc discrepancy found (`128:56`/
`kit.sfx` absent everywhere), one live bug found as a side effect (the
`Percussion` alias - see Phase 0). Nothing further to do here for Phase 1
itself; the script stays in `tools/` for future font additions (e.g. whenever
a user-supplied font like `Essential Keys-sforzando-v9.6.sf2` needs its own
verification pass before its presets get registered).

### SF2 lookup fix

- Add `SoundFont::createInstrumentByProgram(int bank, int program, const char *
  name = 0)` to `SoundFont.h`/`.cpp`: looks up `sf_->getPresetIndex(bank,
  program)`, returns `nullptr` on `-1`, else delegates to the existing
  `createInstrument(size_t, const char *)`.
- `SoundFontFile::getPresetIndex` is already public and already correct
  (linear scan over `presets_` matching both fields) - no change needed there
  beyond exposing it, which the new `SoundFont` method does.

### Registry and resolver

New home: extend `InstrumentProvider` (it already owns instrument lifetime and
is the one object with visibility into every loaded provider) rather than a
new top-level class, since nothing else currently needs to reach a registry
independently of it.

- `instruments_by_name` (existing flat map) keeps serving the one purpose it
  has left after Phase 0's alias-to-path rename: file-specific native names,
  now namespaced (see below). The curated-alias half of it is gone - Phase 0's
  table above already renamed every entry to its taxonomy path, so by the time
  Phase 1 lands, `GenericInstrument::prepare()`'s plain-name lookup is only
  ever serving native names (or a not-yet-migrated string someone hand-typed
  after Phase 0 shipped, which resolves to nothing either way, same as today).
- New: `std::unordered_map<std::string, shared_ptr<Instrument>>
  paths_by_taxonomy_path` (or reuse one map with a value struct carrying a
  priority/source tag - a second map is simpler and keeps native-name lookup
  untouched). Populated by a new `registerPath(string path, shared_ptr<Instrument>)`
  method, callable by any provider - `loadSoundFont()` calls it once per
  `{bank, program, path}` table row that actually resolves
  (`createInstrumentByProgram` returning non-null), and nothing else calls it
  yet, but the method itself has no SF2-specific assumption in its signature.
- Resolution (`resolvePath(string path)`), two passes:
  1. **Walk-up on the literal request**: try the full path, then
     progressively shorter prefixes (drop the last `.segment` each time) against
     `paths_by_taxonomy_path`. Return the first hit.
  2. **Default-table redirect**: if pass 1 found nothing, try the same
     shrinking prefixes against a second static table (the "Defaults for
     general requests" table from the doc, e.g. `piano` ->
     `piano.acoustic.grand`), and if a prefix matches, recursively resolve the
     table's target path (which is expected to itself satisfy pass 1).

  This two-pass shape is a design decision the prompt didn't fully specify (it
  describes walk-up and the defaults table as two separate mechanisms but not
  how they compose) - flagging it explicitly for approval. It's what makes
  every row in the "Defaults for general requests" table actually reachable:
  a bare `piano.electric` request has no exact registration (only
  `piano.electric.grand`/`.tine`/`.fm` do) and would dead-end under walk-up
  alone; the default-table redirect is what saves it. Concretely this means
  `resolvePath` is:
  ```
  resolve(path):
    for prefix in shrinkingPrefixesOf(path):       // path, path-minus-last-segment, ...
      if paths_by_taxonomy_path.has(prefix): return paths_by_taxonomy_path[prefix]
    for prefix in shrinkingPrefixesOf(path):
      if defaults.has(prefix): return resolve(defaults[prefix])
    return nullptr
  ```
- `GenericInstrument::prepare()` changes from a single
  `provider.getInstrumentByName(getName())` call to: try the existing
  name-based lookup first (covers native names and any surviving legacy
  aliases), then `provider.resolvePath(getName())` if that misses, then
  `default_instrument` if both miss. **Not** simply "call
  `getInstrumentByName()`, then `resolvePath()` if that misses" -
  `getInstrumentByName()` already silently falls back to `default_instrument`
  on any miss (`InstrumentProvider.h`'s existing behavior, confirmed while
  implementing Phase 0's rename - see `CLAUDE.md`'s corrected note on the
  fallback instrument), so chaining on top of its return value would never
  actually reach `resolvePath()` at all: a literal-name miss doesn't look
  different from a real match once it's already substituted the default. The
  literal-name check has to be a real "is this exact string registered"
  query (i.e. a plain `instruments_by_name.count(name)`/`.find(name)`, not
  `getInstrumentByName()`), with the existing default-instrument fallback
  applied exactly once, after both attempts, not folded into the first one.

### Collisions

Proposed rule: **last registration wins**, plain overwrite on
`paths_by_taxonomy_path[path] = instrument` in `registerPath()`. No priority
tiers, no merge. This falls directly out of provider *load order*, which the
caller already controls (`Controller.cpp` decides what gets loaded and in what
sequence) - "load the system GM font, then load the user's font" already
expresses the desired priority without the registry needing to know about
providers at all. Simple, and matches the existing `createAll()` fallback's
own asymmetric convention (`if (!instruments_by_name.count(...))`, i.e.
*first*-registered-wins there) closely enough to be predictable: native names
protect curated aliases by going second and losing; taxonomy paths protect
richer/later-loaded fonts by going second and winning. Not implementing
multi-font loading now (only one font is loaded today), just confirming the
rule doesn't need to change when it happens.

### Native preset names, namespaced

Prefix every `createAll()` registration key with `"native:"` (e.g.
`"native:Acoustic Grand Piano"`), stored in the same `instruments_by_name` map
- a colon can't appear in a dotted taxonomy path, so this makes collision with
`paths_by_taxonomy_path` structurally impossible rather than merely
convention. `GenericInstrument::prepare()`'s plain-name lookup keeps looking up
the *unprefixed* string (for legacy aliases) but has no reason to reach
`native:`-prefixed entries directly from a literal `name=` value - the prefix
exists so a human or a future feature can ask "give me the SF2 file's own name
for this patch" unambiguously, not to be typed into a song file by hand. This
does mean any song currently naming an instrument by its raw native SF2 preset
name (anything not in Phase 0's ~25-entry curated-alias table, e.g. a song
that names an instrument `name="Tuba"` or `name="Glockenspiel"` directly)
stops resolving unless it's also rewritten to `native:<name>`. Phase 0 doesn't
cover this - it only renames the curated aliases you asked about. If you want
it done too, now that a real `FluidR3_GM.sf2` is available to check native
names against, say so and I'll fold it into the same rewrite pass (it's the
same mechanism, just a longer, auto-derived table instead of a 25-row
hand-checked one) - flagging it here rather than assuming.

### Identity

Nothing new. `Track`/`SongObject`'s existing `id_` (user-assignable, persisted
as the `id` attribute) and `internal_id_` (process-lifetime, `SongObject.h:23-34`)
already give every `<instrument>` pool slot exactly the per-occurrence identity
Phase 2 needs to distinguish two same-path instruments with different
overrides - confirmed above, this is also exactly the storage location Phase 2
uses.

### Old name strings - resolved, moved to Phase 0

The prompt frames this as an open question ("decide whether those are migrated
... or left resolving through the file-specific native-name namespace"), but
given the design above, "leave them" wasn't actually available without extra
compatibility code: the curated aliases come from a runtime registration table
the prompt explicitly says to delete ("Replace the hardcoded alias table with a
single `static const` table of `{bank, program, path}` tuples"), so once that's
gone, any song still holding the old alias string resolves to nothing (falls
back to `default_instrument`, silently) unless something rewrites it. Resolved
by moving the whole curated-alias rename into Phase 0, at your request - see
the rename table there. What's *not* resolved yet, since it wasn't part of
that request: native (non-curated) SF2 preset names - see the note under
"Native preset names, namespaced," just above.

### Forward compatibility

Already satisfied by the design above without extra work: `registerPath()`
takes an arbitrary string and a `shared_ptr<Instrument>`, doesn't touch any
enum, doesn't validate against `docs/instrument-paths.md` - a future
non-GM-mapped font's loader would call `registerPath("piano.acoustic.grand.yamahaC5",
...)` directly, no different from the GM table's own calls. `resolvePath`
never assumes the GM table is the only populator.

## Phase 2 - song-level SF2 generator overrides

### Data model

- `GenericInstrument` gains `std::unordered_map<int, float> generator_overrides_`
  (keyed by SF2 generator id - `8` for `initialFilterFc` - not by name; the
  name<->id table below is a load/save-time concern only, not a runtime one).
  Stored unclamped, exactly as authored - clamping is generator- and
  backend-specific (see below), so it belongs at the point where a specific
  backend interprets a specific generator, not at parse time.
- Name<->id table: a new small header, `src/instruments/SF2GeneratorNames.h`
  (no existing table maps generator *names* to ids anywhere in the codebase -
  `genMetas` in `SoundFont.cpp` is indexed by numeric id only, with the name
  living purely in a same-line comment). One entry for Phase 2:
  `{"initialFilterFc", 8}`. Shared between the XML parser (name -> id, on
  load) and the writer (id -> name, on save) so both directions stay in sync
  by construction rather than by two independently-maintained switch statements.
- Unknown generator name on load (item 5): **preserve unapplied.** Store it in
  `generator_overrides_` keyed by... this is the one place the "name is
  load/save-only" design above needs an exception: an id this table doesn't
  know can't be looked up by id. Store unknown entries in a second, small
  `std::vector<std::pair<std::string, float>> unknown_generator_overrides_`
  (name, value) instead, round-tripped verbatim on save, never consulted by
  any backend. This satisfies both halves of item 5 at once ("preserve
  unapplied" and "backend ignores unhandled generators ignores it silently")
  without the file losing data it can't currently interpret - a song
  authored/edited by a future version that supports more generators
  round-trips losslessly through an older binary. Rejecting the file outright
  was the other option on the table; preserving is strictly less destructive
  and costs one extra small container.

### Serialization

`GenericInstrument`/`Instrument` doesn't own its own parsing loop today (all
children/data blobs are handled by `Song.cpp`'s `parseChildTrack`/
`storeChildTrack`, e.g. `<drumMachine>` for `DrumMachineTrack` - see
`Song.cpp:139-195`). `<generator>` gets the identical treatment:

- `parseChildTrack` (`Song.cpp:197-221`): after the existing
  `dynamic_cast<DrumMachineTrack*>` block, add a
  `dynamic_cast<GenericInstrument*>` block calling a new
  `loadGeneratorOverrides(GenericInstrument &, XMLElement &)`, iterating
  `element.FirstChildElement("generator")`/`NextSiblingElement("generator")`,
  reading `name`/`value` attributes, looking up the id via
  `SF2GeneratorNames.h`'s table, and populating `generator_overrides_` or
  `unknown_generator_overrides_` per the above.
- Add `"generator"` to the existing child-skip check
  (`if (string_view(it->Name()) == "drumMachine") continue;` becomes an
  `||`), so the generic child-track recursion doesn't try to `createTrack()`
  a `<generator>` element and fail.
- `storeChildTrack` (`Song.cpp:223-239`): after the existing recursive
  child-track loop, add the `GenericInstrument` counterpart, appending one
  `<generator name="..." value="..."/>` per entry in both maps.
- No change to `Instrument::loadParameters`/`storeParameters` (`Instrument.h`)
  - generator overrides are element children, not attributes, matching the
    prompt's explicit "repeatable child element" decision.

### Application point

Exactly the two `voiceRegion_->initialFilterFc` use sites identified above
(`SoundFont.cpp:984,988,1339`), and nowhere else:

1. `Instrument::playNote(...)`'s virtual signature
   (`Instrument.h` doesn't declare it; the pure virtual lives on `Track.h:93`,
   overridden identically-shaped by every leaf: `Oscillator`, `Noise`, `LFO`,
   `FileInstrument`, `NoteMultiplier`, `GenericInstrument`,
   `SoundFontInstrument`, plus `TapeDegradation`/`Track`'s own default body)
   gains one new trailing parameter with a default, e.g.
   `const GeneratorOverrides & overrides = {}` (a small wrapper around the two
   maps above, or just pass the maps directly - a named struct reads better at
   call sites). Defaulted, so every existing call site that doesn't care
   (`InstrumentTrackState.h:198`, `ArpeggiatorState.cpp:257`, every leaf's
   internal modulator-forwarding call) needs no change at all.
2. `GenericInstrument::playNote()` (`GenericInstrument.h:14-32`) is the one
   real producer: it forwards `overrides` (its own `generator_overrides_`) to
   `concrete_instrument_->playNote(...)` instead of accepting the incoming
   parameter - a `<generator>` element is a property of *this* pool slot, not
   something a caller passes in, so the parameter it forwards down is its own
   member, not the one it received (mirrors how `detune` is transformed by
   `getHarmonic()`/`getSubharmonic()` before forwarding, not passed through
   unchanged).
3. `SoundFontInstrument::playNote()` (`SoundFont.cpp:1801`) threads `overrides`
   into the `SoundFontVoice` constructor (`SoundFont.cpp:1874`'s call site
   gains one argument).
4. `SoundFontVoice`'s constructor (`SoundFont.cpp:926`) stores it as a member
   (`GeneratorOverrides overrides_;` or similar - small, copied once per voice,
   negligible cost).
5. The two use sites become, e.g.:
   ```cpp
   auto it = overrides_.find(8 /* initialFilterFc */);
   int fc = it != overrides_.end()
     ? std::clamp(static_cast<int>(it->second), kFilterFcMin, kFilterFcMax)
     : voiceRegion_->initialFilterFc;
   ```
   with `kFilterFcMin = 1500`/`kFilterFcMax = 13500` pulled out as named
   constants next to `genMetas`'s existing `GEN_INT_LIMITFC` case
   (`SoundFont.cpp:384`, currently inline literals) so both the ordinary
   preset-generator path and the new override path clamp against the same
   spec-derived numbers instead of two independently-typed copies of `1500`/
   `13500`.
6. Every other leaf (`Oscillator`, `Noise`, `LFO`, `FileInstrument`,
   `NoteMultiplier`, `TapeDegradation`) accepts and silently ignores the new
   parameter (never reads it) - this *is* item's "backend ignores unhandled
   generators" contract, made structural: a backend that never looks at
   `overrides` at all is trivially compliant.

### What this implies about precomputed generator resolution (item 4)

Per-region generator resolution (`tsf_load_presets`/`tsf_region_operator`'s
merge-and-clamp pass) is fully precomputed once at font-load time, into the
shared `tsf_region` the whole file's `presets_` vector owns - confirmed above.
An override therefore cannot hook into that resolution pass at all (it runs
once, at load, long before any song-specific override exists) - it has to be a
second, independent substitution applied per-voice, downstream of it, reading
`voiceRegion_->initialFilterFc` as the "no override" fallback exactly as today.
This is what makes the two-use-site substitution the only correct injection
point, not a stylistic choice.

### No-override bit-identical guarantee

Falls out of the design rather than needing a separate mechanism:
`generator_overrides_.find(8)` on an instrument with no `<generator>` children
is empty, `it == overrides_.end()`, so the ternary above takes the
`voiceRegion_->initialFilterFc` branch - byte-identical to current code.

## Phase 3 - confirm nothing earlier blocks it (not implementing)

Sketch only, per the prompt.

- `<instrumentMap>` needs a parallel top-level pool to `<instruments>` (a new
  `Song::instrument_maps_`), or the existing `instruments_` vector could carry
  both kinds of pool entries if `Track`/`Instrument` grows an
  `InstrumentMap` sibling type discriminable at the C++ level - either is
  viable; nothing in Phases 0-2 forecloses either, since Phase 2 didn't touch
  `Song::instruments_`'s element type. Leaning toward a separate pool/vector,
  since "a pitched track cannot play one" (prompt's own constraint) is easiest
  to enforce as "wrong vector, wrong lookup function" rather than a runtime
  type check on every lookup.
- `registerPath`/`resolvePath` (Phase 1) are general enough to host a second,
  independent registry for `kit.`-rooted paths (a second
  `unordered_map<string, shared_ptr<InstrumentMap>>` inside
  `InstrumentProvider`, walked the same way) - the "walk-up cannot cross
  between `kit.` and the pitched tree" requirement falls out for free from
  using two separate maps rather than one, no extra guard code needed.
- Percussion symbol table needs to move from `src/model/Note.h:119-197` into
  its own shared header (e.g. `src/model/PercussionSymbols.h`) before
  `InstrumentMapTrack`/serialization and any rendering/UI code can share it -
  a real but small extraction, not blocked by anything in Phases 0-2.
- `PercussionTrack` -> `InstrumentMapTrack` rename is the same shape as
  Phase 0's element rename (a class/element rename plus a song-file
  mechanical pass), so it can reuse that phase's verification approach
  (render-diff before/after) directly.
- Nothing in Phase 2's generator-override design assumes pitched-only -
  `GeneratorOverrides` is keyed by generator id, not by anything path/taxonomy
  specific, so an `<instrumentMap>` entry could in principle carry the same
  child element later. Not needed for Phase 3's stated scope (whole-kit
  loading only), just noting it isn't blocked.

## Files touched, per phase

**Phase 0**: `src/instruments/GenericInstrument.h` (1 line),
`src/model/Song.cpp` (1 line + 1 comment), `src/instruments/SoundFont.cpp`
(1 comment), `src/instruments/InstrumentProvider.h` (rewrite the ~25-entry
alias block's string literals in place - same call structure, new names, plus
collapsing the 3 now-duplicate pairs), every `songs/*.xml` +
`songs/backup/*.xml` + `tests/fixtures/sf2_reverb_send.xml` (both the element
rename and the alias `name=` rewrite, mechanical), `docs/instrument-paths.md`
(the taxonomy doc itself - folded into Phase 0 so it merges alongside
everything that depends on it, rather than sitting uncommitted indefinitely;
its `kit.sfx` row's caveat text corrected once `tools/verify_sf2_taxonomy.py`
showed it unimplemented in every font checked - see "Status"),
`tools/verify_sf2_taxonomy.py` (already written and run - see "Status"), one
throwaway before/after render-diff script (not committed, or `tools/` if you
want it kept for future migrations of this shape).

**Phase 1**: `src/instruments/SoundFont.h`/`.cpp` (new
`createInstrumentByProgram`), `src/instruments/InstrumentProvider.h`
(replace the by-then-already-renamed alias block with the `{bank,program,path}`
table + `registerPath`/`resolvePath` + defaults table + native-name prefixing -
a second pass over the same block Phase 0 already touched), `src/instruments/
GenericInstrument.h` (`prepare()`'s fallback order), new
`tests/InstrumentResolverTests.cpp`. Native (non-curated) preset-name
migration - a further song-file pass plus the `native:` prefixing - only if
you want it folded in too (see "Native preset names, namespaced" above);
otherwise it's not part of Phase 1's file list.

**Phase 2**: new `src/instruments/SF2GeneratorNames.h`, `src/model/Song.cpp`
(`parseChildTrack`/`storeChildTrack` additions), `src/instruments/GenericInstrument.h`
(`generator_overrides_`/`unknown_generator_overrides_` members + accessors),
`src/model/Track.h` + every `playNote()` override listed above (new trailing
parameter - `Track.h`, `GenericInstrument.h`, `SoundFont.cpp` x2
(`SoundFontInstrument`, `SoundFontVoice`), `Oscillator.h`/`.cpp`, `Noise.h`/`.cpp`,
`LFO.h`/`.cpp`, `FileInstrument.h`/`.cpp`, `NoteMultiplier.h`/`.cpp`,
`effects/TapeDegradation.h`/`.cpp`), extend `tests/SF2ModulatorTests.cpp`.

## Tests (item 7)

1. **Dual-font path resolution** (`tests/InstrumentResolverTests.cpp`, new):
   build two synthetic `.sf2` fixtures with `writeMinimalSf2`
   (`tests/SF2ModulatorTests.cpp`'s existing helper, reused as-is -
   `PresetSpec::program`/`bank` already support arbitrary values, so a
   deliberate gap is just a `PresetSpec` list that skips a program number).
   Font A: contiguous programs 0..N. Font B: same logical presets, but with a
   gap inserted before the one under test (so its sorted-by-(bank,program)
   array index differs from its program number). Register both through
   `createInstrumentByProgram`, resolve the same path against each, assert
   both return an instrument whose *name* (or some other font-independent
   marker set in the fixture) matches - i.e. the gap doesn't shift which
   patch a path binds to.
2. **`getPresetIndex`/`createInstrumentByProgram` returns `nullptr` for an
   absent program** (a font missing GM program N entirely) - and that
   registration skips it without crashing or registering an empty instrument.
3. **Walk-up resolution**: register only `piano.acoustic.grand`, resolve
   `piano.acoustic.grand.yamahaCfx`, assert it returns the registered
   instrument.
4. **Default-table redirect**: register only `piano.electric.tine`, resolve
   bare `piano.electric`, assert it redirects and resolves.
5. **Collision**: register the same path twice with two distinct instruments,
   assert the second wins.
6. **Load/save round-trip** (extend `tests/SF2ModulatorTests.cpp` or a new
   `GeneratorOverrideTests.cpp`): a song with one `<instrument>` carrying one
   `<generator name="initialFilterFc" value="9000"/>`, save, reload, assert
   the reloaded instrument's override map has `{8: 9000.0f}`.
7. **Clamping**: value `500` (below spec min) round-trips/applies as `1500`;
   value `99999` applies as `13500`. Since the stored value is unclamped (see
   design above), this is an applied-value assertion, not a stored-value one -
   render two notes (one with the extreme override, one with a hand-set
   in-range equivalent at the clamp boundary) and assert the outputs match,
   the same render-and-compare style `sf2_channel_pressure_heuristic_end_to_end`
   already uses.
8. **No-override bit-identical**: render the same fixture preset through
   `SoundFontInstrument` with an empty `GeneratorOverrides{}` vs. the current
   (Phase-2-less) code path side by side isn't possible post-merge (only one
   code path will exist), so instead: render twice, once via
   `GenericInstrument` with no `<generator>` children and once via
   `SoundFontInstrument::createInstrument` directly (bypassing the wrapper
   entirely) for the same preset/note, assert byte-identical output - proves
   the wrapper's default-constructed `GeneratorOverrides{}` truly changes
   nothing.
9. **Unknown generator name preserved unapplied**: load a song with
   `<generator name="totallyMadeUp" value="5"/>`, assert it doesn't reject the
   file, doesn't affect rendered output, and round-trips byte-identical
   through a save.
10. **Audible-difference check**: render the same note through the same
    preset with `initialFilterFc` at spec-default (13500) vs. 9000, assert
    measurable high-frequency energy loss in the low-cutoff render (an FFT
    magnitude comparison above ~1.5kHz, reusing `dsp/RealFFT.h` the same way
    `dsp/SpectrumAnalyzer.h` does) - this is the one place an automated test
    can stand in for the manual listening check, since "does a lowpass filter
    measurably remove high-frequency energy" is a property, not a subjective
    judgment. The *musical* claim ("this makes a GM piano sound better for
    31-EDO") stays a manual listening check regardless - no test can assert
    that, and the prompt's own manual-verification section already plans for
    it.

## Things that make this harder than the prompt's framing suggests (item 8)

- **The shared-backend-instance sharp edge** (see "What's already true," above)
  is the single biggest thing the prompt doesn't call out explicitly: a naive
  "store the override on the resolved `Instrument`" implementation would
  silently leak overrides across every pool slot sharing a taxonomy path in
  the same song. The `playNote()`-parameter-threading design exists
  specifically to avoid this, at the cost of touching every `Track::playNote`
  override in the codebase (9 files) for what is, from a single generator's
  point of view, a small feature.
- **Old `name=` strings broke harder than "left resolving through the native
  namespace" implies** - resolved now (folded into Phase 0, at your request),
  but worth recording that this wasn't a free-standing open question with a
  deferrable answer: the original Phase 1 design (delete the alias table) made
  it a forced migration whether or not it got explicitly scheduled.
- **The alias table encoded two real, silent bugs** (the `"Electric Piano"`/
  `"Viola"` mislabels, and the `Percussion`/index-160 bug affecting 23 songs) -
  none of these were things the original prompt asked me to go looking for;
  they surfaced only because turning "rename this string" into a concrete
  per-row table forces you to look at what each alias is actually bound to.
  Worth keeping in mind for the rest of this project: any further "just
  rename"-shaped step is liable to surface more of these.
- ~~Verifying the taxonomy needs files this sandbox doesn't have~~ - resolved;
  all three fonts are installed at `/usr/share/sounds/sf2/` on this machine
  after all, see "Status" at the top.
