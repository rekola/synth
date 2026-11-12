---
paths:
  - "src/model/**"
  - "src/doc/**"
  - "src/state/**"
  - "src/playback/**"
  - "src/Controller.*"
---

# Song model, document, clips and live playback

## Shape of the model

- `Song` holds one `Arrangement`: a single timeline keyed by absolute row
  (`<arrangement>` in the file) whose length is where its content ends. Per
  track it holds three kinds of content: instance events (which `Clip`
  starts at a row, or an explicit stop; a looping clip plays on until the
  track's next event), an inline `Pattern` (the track's own notes/commands,
  always an independent copy, never shared like a clip), and a
  `SampleTrack`'s merged background audio bed. Plus the song-wide objects,
  by absolute row: locators (named markers), tempo changes and time signature
  changes (`<tempo row bpm>`, `<signature row timeSignature>`).
- `src/state/` mirrors the model with cheaply-resettable `*State` objects.
  `src/playback/` has `Player` (the sequencer and its event vocabulary) and
  `ClipPlayer` (clip launching, Controller-owned).

## Edits, undo and the document

- Every write to the song happens inside a `Song::Edit` scope, one per user
  action (a key, a command, a pad press, a whole live take). The scope bumps
  the version and publishes the audio thread's copy when the outermost one
  closes, so an action's writes become visible together. Scopes nest;
  `discard()` is for one that wrote nothing. `CONTENT` edits (note, command,
  velocity, delay) count in the minor version, everything else in the major.
  A `Song` setter or a write through a view opens its own `Edit` if none is
  open, so a loop of writes should sit in one. `SYNTH_VERIFY_CONTENT=1` makes
  the UI report published content that no longer matches the model.
- The song lives in a DOM (`src/doc/`): nodes with stable ids, four journaled
  primitives, one append-only journal per song capped at
  `Song::kJournalLimit`. Scalars (tempo, swing, signatures, tuning, key,
  scale, room), scenes, locators, the whole score, tracks, instrument pool and
  send bus are all in it; scalars and score are declared once in
  `model/SongSchema.h`/`ScoreSchema.h`. `Song`'s accessors stay the API.
- The score is edited through short-lived handles (`PatternView`,
  `ClipView`/`ClipList`, `ArrangementView`) that read like pointers but hold
  no content, so they cannot go stale. Reads return values, never references
  into storage: never bind a reference to an element of a view's result. An
  invalid or default handle is inert. `Pattern`, `Clip`, `Arrangement` remain
  plain value types for the published copy, the clipboard and loaders.
- Sample audio is not in the document: a "sample" node names an asset in
  `SampleStore`, which keeps the per-layer `SampleContent`, the time-stretch
  work and the overdub mix. Never change an asset's audio in place once it is
  in a clip — history may bring it back.
- An `Edit` with `Origin::SYNC` mirrors the audio thread (scene-launch tempo,
  glides, running bars): journaled but untracked, never undone on its own.
  Opening a file or constructing a song leaves no history.
  `SYNTH_VERIFY_DOCUMENT=1` replays every transaction to check its undo record.
- **Undo** (`doc/UndoHistory.h`, `Song::undo()`/`redo()`, the `undo` and
  `undo-redo` commands) is the Emacs policy over the journal: an undo appends
  the inverse of an earlier entry, so nothing is dropped and undoing an undo
  is a redo; the run of undos is broken only by a tracked edit (cursor moves
  are not edits); `SYNC` entries are passed over. Undo does nothing while an
  undo group is open, which is how a live take is one step
  (`Controller::updateUndoGroup()` holds a group open while any clip take or
  auto-record is in flight; note capture armed with the transport rolling
  counts). Held note keys keep a group open too, so a chord is one step.
  Typing edits amalgamate (`Song::Edit`'s `typing` flag) until a non-typing
  key, an idle gap or `kTypingRunLimit` breaks the run. After an undo/redo
  `Song::lastUndoPlace()` names where it landed and the commands move the
  cursor there. An undo/redo whose change is in a clip that is playing,
  queued or recording is refused (`nextUndoPlace()`/`nextRedoPlace()` say
  where it would land first).
- Tracks, instruments and bus effects are nodes whose type is their song-file
  element name and whose string properties are exactly the attributes their
  `loadParameters()`/`storeParameters()` use (`model/TrackNodes.h`), so those
  classes know nothing of the document. An attribute no class knows is
  dropped on load. What the program sees are immutable `Track` objects
  compiled by `TrackCompiler` when an `Edit` closes; only nodes that changed
  (and their ancestors) are rebuilt. **A `Track` reference is good only until
  that track is edited** — look it up again by internal id. Change a track
  with `Song::editTrack(id, fn)` (`fn` runs on a scratch copy); add, remove,
  reorder with `addTrack()`/`removeTrack()`/`addInstrument()`/
  `removeInstrument()`/`setBusSlotKind()`; never build a track's sub-tracks
  after adding it.
- The audio thread never reads the live arrangement, clips or tracks: it
  reads `PlaybackContent` (`model/PlaybackContent.h`), an immutable copy with
  the compiled tracks attached, built by `Song::publishContent()` and handed
  over by `ContentPublisher` without locks; displaced copies are freed on the
  UI thread only. `SongState::renderBlock()` holds one `readContent()` per
  block and keeps no pointer past it (compare `SampleContent::identity()`,
  not addresses, across blocks). A song a `Controller` owns is in published
  mode; a song rendered on one thread (offline renderer, tests) copies on
  every read, so writes there need no `Edit`. State that outlives a block
  must not point into the tracks: keep an internal id and settings by value.

## Clips

- A `Clip` (`Song::getClips(track_id)`/`addClip()`/`ensureClipAt()`) is
  reusable, shareable content for one leaf track: a single `Pattern`, or for
  a `SampleTrack` one or more `SampleContent` layers (a `std::deque`, so a
  layer's address survives a later append — the render-time pending-start
  path holds a raw pointer across a block). Overdubbing appends a layer,
  never replaces. What plays is `Clip::getMixedContent()`: layer 0 alone, or
  a cached pre-mixed sum rebuilt off the audio thread once a take is final,
  never computed on a trigger.
- Editing a clip through any placement updates every placement at once;
  a track's inline pattern is always an independent copy.
- A gap in a clip list is a real, empty `Clip`, not a missing slot:
  `ensureClipAt()` pads earlier positions with fillers, which get no id until
  given content. **Every "is this slot populated" check reads content
  (`!clip.isEmpty()`), never bounds**, and a filler is never erased — that
  would shift every later clip's index and misalign scene rows across tracks.
- Persisted as a top-level `<clips>` element beside `<tracks>`/
  `<arrangement>`: one `<clip track id name loop length>` per clip in order;
  an empty filler is a `<clip>` with no `<pattern>` (and `stop="false"` if its
  stop button was removed); a SampleTrack clip holds one `<sample file>` per
  layer, sidecar `.wav`s named by `sampleSidecarPath()` (`<id>.wav`,
  `<id>_2.wav`, ...). Authored in-app by `PatternEditor::copyToClip()`.
- `Command`'s first character is `Z` (global), `Y` (this engine's reserved
  namespace) or the track's chain-position digit — always `0` today, `-`
  accepted as a synonym; the digit is reserved for addressing a parent track,
  so a clip will never need a second track's content.

## Live View (clip launching)

- Launched clips play inside the one transport, per track: a launch takes
  the track over from the arrangement, a stop takes it over too (silent),
  `back-to-arrangement`/`track-back-to-arrangement` hand it back. The audio
  thread owns this (`SongState::queueLaunch()`, per-track `LiveTrackInfo`):
  **every change applies on the first row of the transport's next bar**, even
  the first launch into silence; rewind to start from the top. Launched clips
  advance on a live clock (rows played, unmoved by seeks or pattern breaks);
  a taken-over track ignores its arrangement content and automation (pattern
  breaks aside). Launching with the transport stopped starts it.
- The transport toggle only pauses (an interim choice; what happens to
  sustained voices is open): launched clips keep clip and row, queued
  changes and takeovers stay, play resumes them. A note take survives a
  pause, a sample take ends. Stopping clips is its own action (stop pads, the
  master column's stop row, back-to-arrangement).
- `ClipPlayer` is the one path for the Launchpad, the clip grid and the
  launch commands: it sends `QUEUE_LAUNCH` with a sequence number, predicts
  the result in `PlaybackInfo` until a snapshot catches up (the same
  stale-snapshot rule as the edit position), and keeps the clip takes,
  resolving them on bar rows seen in snapshots (`tick()`, once per UI frame).
  A finished take loops back on the bar its stop resolved on. Pending changes
  are never gated on the triggered clip's own loop length and never
  immediate.
- A launch never toggles: re-pressing the active pad relaunches from row 0
  at the next boundary; a scene launch restarts a playing scene the same
  way. A stop releases voices through their natural `stopNote()` tail
  (`InstrumentTrackState::stopAllVoices()`), never hard-cut.
- Monitoring (`LeafTrack::Monitor`, `cycle-monitor`): In always, Off never,
  Auto while armed (and for a note track while nothing is armed, which is
  how pad/MIDI/keyboard entry sound by default). Only live note-ons are
  gated, never clip playback, edit auditions or releases. A monitoring
  SampleTrack hears the live input: `Controller::syncMonitoring()` sends
  `SET_TRACK_MONITORING`, `Player` passes each captured block through a
  `dsp::MonoFifo` to the next playback block, and `SampleTrackState` plays it
  as the track's third fixed voice through its position, sends and effects.
  Only the active buffer hears the input.
- A region's effect commands always go to the track's background pattern,
  where recorded automation lives; playback reads a row's commands from
  there first, then from a placed clip's pattern, so the clip's own command
  wins on a conflict (`SongState::applyRowCommands()`, `docs/commands.md`).

## Song file, scenes, bars

- A `Note` is a `Kind` (none, on, off, aftertouch) plus one 7-bit velocity.
  **Never tell an OFF by velocity 0**: velocity 0 is a real note-on that starts
  in exact silence (`velocity::kSilentGainDb`). An OFF's velocity is its *lift*, fixed
  when the OFF arrives and handed to the voices
  (`VoiceState::setReleaseVelocity()`), where it scales release times. File:
  `<note>`'s `velocity` attribute is the note-on's velocity, the OFF's lift or
  the aftertouch pressure (neutral 64 omitted, except for aftertouch); the text
  is the note, `OFF` or `???` (a lost note: it began here but its pitch is
  gone; it makes no sound and ends the column's previous note) or `~~~` (a lacuna: a
  span of missing notes, the same way) — neither has a velocity or a delay, so the
  file writes neither and ignores them if present — and aftertouch has none, like its empty note column.
- A track names its pool instrument by id (`instrument="i1"`); the
  arrangement places a clip with `<instance track row clip/>`, a stop being
  an instance with no `clip`. There is no reader for earlier file forms: the
  project has no files to keep, so a format change converts the repo's songs.
- **Scenes** (`<scenes><scene name tempo timeSignature/>`, by position like a
  clip list): a clip-list row shared by every track with optional name,
  tempo and signature, edited in the clip grid's Master column with F2;
  `scenename::extract()` splits "Waltz 3/4 90 BPM", "0 BPM"/"0/4" clear.
  `ClipPlayer::launchScene()` sends one `QUEUE_SCENE_CHANGE`, applied on the
  bar the clips launch on: the tempo becomes the song tempo, the signature
  the running signature. `followAction` on a scene (`model/SceneFollow.h`) makes
  the audio thread launch the next scene, or stop, once the scene's longest
  clip has played (`SongState::followScene()`); a scene change cancels it.
  Rhythm-library templates carry a signature that Add
  to Song gives a scene that has none. `docs/scenes.md`.
- **Bars** (`TimeSignature.h`, `BarGrid.h`, `docs/time_signatures.md`): a row
  is a sixteenth; n/d is n*16/d rows per bar and 16/d per beat, three times
  that in compound meter (6/8, 9/8, 12/8). The song has a starting signature
  (4/4 unless set, `set-time-signature`) and the arrangement's signature
  changes re-bar it from their rows; `Song::getArrangementBars()` is an
  `ArrangementBars` (bars numbered across the changes). Tempo changes work
  the same, applied by `SongState` when the row is reached (`rowTempo()`;
  a `ZTxx` on the row wins, a seek rescans). Recording an arrangement
  (`ClipPlayer::recordSceneChange()`) writes a launched scene's tempo and
  signature as changes and `mirrorSceneChange()` then leaves the song tempo
  alone. A launched scene's signature is the
  *running signature* (`RunningBars`, saved as `transportTimeSignature`/
  `transportBarOrigin`) overriding the song's from its origin until another
  scene or Back to Arrangement. The audio thread owns the running signature
  and scene tempo (`SongState`'s `pending_scene_`/`running_bars_`/`barsAt()`,
  applied on the bar, sample-exact with the launches); the UI's `Song` copies
  are mirrored from the snapshot by `Controller::mirrorSceneChange()` once per
  `getSceneSeq()`, so an older snapshot never overwrites a later edit.
  UI-thread code reads `Song::getBarsAt()`; arrangement code reads
  `getArrangementBars()`. **A bar number is never `row / rows_per_bar`.**

## Pattern effect commands

`docs/commands.md` lists them, split into Implemented (`ZBxx` pattern break;
`ZTxx` tempo, the playing tempo only - a seek recomputes it, `SongState::rescanTempo()`,
and `Controller::mirrorSceneChange()` mirrors only a scene's tempo;
`0Pxx` azimuth; `0Lxx` volume; the real-time `Y` commands `YLxx`/`YRxx`
azimuth slide and `YMxy`/`YAxy`/`YBxy`/`YZxy` volume/send A/send B/azimuth
with a glide duration; `SongState.h`'s command loop) and Planned (stored,
no-ops at playback). Its "Source" column names where each letter came from
(an existing tracker's reference or this codebase's own choice) — checked
deliberately, not invented. A mnemonic's trailing hex argument parses a
non-hex character as 0 rather than rejecting it.
