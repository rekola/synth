# Remove FileInstrument; give SampleTrack real sample-clip playback

## Context

Four asks, folded into one plan since they're one connected change:
1. Remove `FileInstrument` (confirmed dead code, see Part 1).
2. Make `SampleTrack` actually play audio - today it exists as a `LeafTrack`
   shape (mute/solo/position/sends, a dedicated placeholder column in
   `PatternEditor`/`SongStructure`) with no playback state behind it at all.
3. Remove the leftover "a `Note` written into the pattern launches audio"
   design - a half-built mechanism, not the `DrumMachineTrack`/percussion
   note-triggering that stays (see Part 2 for exactly what's being removed).
4. A `SampleTrack` holds multiple audio files (recorded or loaded from
   disk), mono-only (auto-downmixed if not).

Two design forks were resolved with the user before writing this plan:
- **Triggering model**: clip-list based, Session-view launched - not
  drum-machine-style note-mapping. Each audio file is one of `Song::
  getClips(track_id)`'s entries for that track (the same `Clip`/clip-list
  mechanism `InstrumentTrack` already uses for its own reusable Pattern
  content, launched via `ArrangementGrid`/Launchpad Session view) - never
  addressed by a pattern-row Note value the way a `DrumMachineTrack` lane
  is.
- **Storage**: sidecar WAV files next to the song XML for now. The user's
  own roadmap already has embedded/binary storage (recordings and cover
  art) as a future direction - Part 3 keeps the file I/O behind one seam so
  that migration later doesn't ripple through every call site.

Everything below was confirmed directly against the current source, not
assumed from the architecture docs. `Clip`/`Song::getClips()` (`src/model/
Clip.h`/`Song.h`) is the real, already-implemented mechanism - CLAUDE.md
itself used to describe an earlier, superseded shape ("pattern pool"/
`getPooledPatterns()`) that doesn't exist in the source; that's since been
corrected there, and this plan uses the same `Clip`/clip-list terminology
throughout.

## Part 1 - Remove `FileInstrument`

Confirmed genuinely dead: `FileInstrument`/`FileInstrumentVoice`
(`src/instruments/FileInstrument.h`/`.cpp`) are declared and fully
implemented but never constructed anywhere (`grep`'d - zero
`make_unique<FileInstrument>`/`new FileInstrument` call sites, and no
`createTrack()`-style factory entry in `Song.cpp` either). It only survives
as a name cited in four comments as an example of the "leaf instrument"
category:
- `src/instruments/Instrument.h` (`cloneWithOverrides()`'s doc comment)
- `src/model/Track.h` (three spots: `playNote()`'s doc comment,
  `getDefaultExtent()`'s doc comment, and the `needs_decorrelation` param
  comment)
- `src/model/SendLevels.h` (one spot)
- `src/state/VoiceState.h` (one spot)

Work:
- Delete `src/instruments/FileInstrument.h` and `.cpp`.
- Remove `src/instruments/FileInstrument.cpp` from `CMakeLists.txt`
  (currently listed on its own line).
- Reword each of the four comments above to drop `FileInstrument` from its
  example list (they each name several sibling classes - just remove this
  one, don't restructure the comment otherwise).
- `FileInstrumentVoice`'s actual playback logic (click-safe start-at-
  sample-0, the decorrelation start-delay derivation, raw sample-by-sample
  copy through `encodePosition()`) is not simply thrown away - Part 5
  repurposes it as the basis of the new sample-clip playback voice, since
  it's already exactly "play a mono `AudioBuffer` at a track's position,
  ignoring pitch."

## Part 2 - Remove the note-launches-audio remnant

This is `UI::handleRecordEvent()` (`src/ui/UI.cpp:838-846`) and the
`Controller` recording state behind it - confirmed non-functional, not
just legacy-but-working:

```cpp
void UI::handleRecordEvent(RecordEvent & ev) {
  if (getController().isRecording()) {
    setStatus(...);
    getController().addToSample(ev.getData());
    auto & scene = song.getScene(info.getPatternIndex());
    scene.setNote(info.getRowIndex(), getController().getRecordingTrackId(), 0, Note(1));
  }
}
```

Confirmed by tracing the whole flow:
- `Controller::stopRecording()` (`Controller.h:274`, resets `current_sample`)
  is never called from anywhere except the unrelated `AlsaAudio::
  stopRecording()` (device teardown) - so once `add-sample-track`
  (`PatternEditor.cpp:576`) calls `startRecording()`, `isRecording()` stays
  true forever; there is no way to end a take today.
- Every captured audio block therefore writes `Note(1)` into the pattern at
  whatever row happens to be playing *right now*, repeatedly, for the rest
  of the session - not a one-time marker.
- Nothing anywhere reads that `Note(1)` back out. `SampleTrack` has no
  `TrackState`/`createState()` override at all (confirmed: `grep` for
  `TrackType::SAMPLE`/`SampleTrack` in `src/state/` and `src/playback/`
  finds nothing), so real playback never produces any sound for a
  `SampleTrack` regardless of what's in its pattern.

Work:
- Delete the `scene.setNote(..., Note(1))` line and the `isRecording()`
  branch's dependency on it in `UI::handleRecordEvent()`.
- `add-sample-track`'s `startRecording()`/`setRecordingTrackId()` wiring
  (`PatternEditor.cpp:588-597`) is reworked, not just deleted - Part 4
  gives recording a real start/stop lifecycle feeding into Part 3's clip
  model.
- What stays untouched: the mic-input capture plumbing itself
  (`AlsaAudio::startRecording()`/`RecordEvent`, `Player.cpp:418-454`'s
  capture-descriptor gating on `Controller::isRecording()`) - that part is
  a legitimate, working audio-input pipeline; Part 4 reuses it, it doesn't
  replace it.

## Part 3 - Data model: `SampleTrack`, `Clip` audio content, mono handling

**`SampleTrack` becomes a real clip-holding track.** Today it carries one
ad hoc `std::shared_ptr<AudioBuffer> sample` member set at construction
(`SampleTrack.h`) - drop that entirely. `SampleTrack` becomes a
default-constructed `LeafTrack(TrackType::SAMPLE)` like every other leaf
type, and its actual audio content lives in `Song::getClips(track.
getInternalId())` - the exact same clip-list mechanism `InstrumentTrack`
already uses (`Song::addClip()`/`getClips()`, `src/model/Song.h:242-266`).
"Multiple audio files" = multiple entries in that list.

**`Clip` gains audio content via a new `SampleContent` child object - not
flat fields on `Clip` itself, per the user's own correction.** `Clip`
(`src/model/Clip.h`) already models a note-based clip's own content as a
child object (`patterns_by_track_id_`, a `Pattern` per track) rather than
scattering note data across `Clip`'s own attributes - a sample clip's
audio gets the same treatment, its own `SampleContent`
(`src/model/SampleContent.h`), not a pile of `Clip`-level fields
(`sample_`/`in_point_`/`out_point_`/`original_tempo_`/... - an earlier
draft of this plan had exactly that, corrected here). Mirrors the
existing `<clip><pattern>...</pattern></clip>` XML nesting too -
`<clip><sample>...</sample></clip>`, not flat `<clip sample-in="..."
sample-out="...">` attributes (Part 7).
```cpp
// Clip.h
bool hasSample() const;                        // true iff a SampleContent with a real buffer exists
const SampleContent * getSampleContent() const; // nullptr if none
SampleContent * getSampleContent();
SampleContent & getOrCreateSampleContent();     // creates one on first use
```
`Clip` holds this as `std::unique_ptr<SampleContent>` - exclusive
ownership, unlike the buffer *inside* `SampleContent` (below): nothing
outside a `Clip` ever needs to keep the `SampleContent` wrapper itself
alive independently, only the `AudioBuffer` it wraps. Only ever populated
when `leaf_track_id_` names a `SampleTrack`; such a clip's
`patterns_by_track_` map stays empty (mixing note-automation content with
sample content on one clip is out of scope here, not designed).

**`SampleContent` itself:**
```cpp
// SampleContent.h
const std::shared_ptr<AudioBuffer> & getBuffer() const; // nullptr if none
void setBuffer(std::shared_ptr<AudioBuffer> buffer);
```
`shared_ptr`, not `unique_ptr`, for the buffer specifically (unlike
`Clip`'s own `unique_ptr<SampleContent>` above) - Part 5's playback voice
needs to go on reading from it for the duration of a note-on, on the
audio thread, independent of whatever the UI thread does to the owning
`Clip`/`SampleContent` in the meantime (edit, delete, reload) - the same
cross-thread-persistence need every existing buffer-holding leaf voice in
this codebase already has (`Controller::current_sample` itself, and the
class Part 5's playback voice descends from in spirit,
`FileInstrumentVoice`, now removed). `unique_ptr` can't be safely held by
both an owner that might go away and a voice that must not care - a live
voice would need to either copy the whole buffer per note-on (wasteful)
or hold an unsafe raw pointer across threads.

**No song-level sample pool in v1.** Considered and deliberately deferred
rather than built speculatively: `Clip` is already this architecture's
shareable unit (its own doc comment: "a single shared object that can be
placed at more than one position at once ... editing it ... updates
every other placement immediately"), but that sharing is scoped to
*placements of one clip on one track* - `leaf_track_id_` ties a `Clip` to
exactly one track by design. Real cross-track content dedup (two
different `SampleTrack`s playing literally the same recording) would need
either decoupling sample ownership from a `Clip`'s per-track identity
entirely, or a content-addressed pool (keyed by file path or a hash, not
clip id) two clips' `SampleContent`s could both resolve to the same
`shared_ptr<AudioBuffer>` from - and Part 7's sidecar-per-clip-id storage
would need the same content-addressing to actually avoid writing
duplicate `.wav` files, not just the in-memory type changing. No concrete
need for this yet (nothing asked for "reuse this exact take across two
tracks"); build it once one shows up, the same "`InstrumentPool` already
proves the shared-resource pattern works here if it's ever needed"
reasoning without pre-building it. Nothing here forecloses it later - two
`SampleContent`s' `setBuffer()` calls can already alias the same buffer
instance manually even without formal pool infrastructure; a pool would
only be needed to get automatic content-based dedup on load/record, not
to make sharing possible at all. `loadMonoSample()` (below) stays the one
seam such a pool would slot behind later - the same forward-compatibility
shape Part 7 already uses for the binary-storage migration.

**Mono enforcement.** A single small loader, e.g.
`src/audio/SampleFileLoader.h`/`.cpp`:
```cpp
struct LoadedSample { std::shared_ptr<AudioBuffer> buffer; int rate = 0; }; // buffer/rate both null/0 on failure
LoadedSample loadMonoSample(const std::string & path);
```
Reads via libsndfile (already linked; same API `FileInstrument::openFile()`
used and `main.cpp`'s `--render` writer already uses the write side of).
Unlike `FileInstrument::openFile()` (which silently kept only channel 0 of
a multi-channel file, and whose caller never even checked its `bool`
return), this genuinely downmixes - averages all channels into one when
the file has more than one - rather than discarding all but the first.
Its only caller is `Song::open()` (Part 7) - loading a file into a clip
is XML-only in this pass, no UI command of its own (Part 8). Deliberately
returns the file's own native rate alongside the buffer rather than
resampling - see below for why that distinction turned out to matter.

**A missing/unreadable `file` reference is fatal, per the user's own
correction - not a silent, best-effort degradation.** Worth stating why
explicitly, since it cuts against Part 3's own "fallback instrument" style
leniency elsewhere in this codebase (`InstrumentProvider`'s always-
available generic-oscillator fallback for an unresolved instrument name -
see `CLAUDE.md`'s own Run section): that fallback exists because the
system is the one *resolving* a name to *some* instrument on the artist's
behalf, and "something plausible" beats silence when there's no exact
match. A `<clip file="...">` is the opposite situation - the artist named
one *specific, exact* file, not a name to be resolved loosely. Silently
continuing with `hasSample() == false` would let a song "load
successfully" while quietly missing exactly the content it was asked to
contain, discoverable only by noticing it's inaudible later - the same
silent-wrongness `loadMonoSample()`'s own real-failure-surfacing above
was already written to avoid. Matches `parsePatternContent()`'s own
existing failure path a few lines away in `Song.cpp`, which already
aborts the *whole* `Song::open()` on malformed pattern content for the
identical reason (return `false`, logging via `fmt::print(stderr, ...)`,
same as that call site) - a missing sample file gets the same treatment,
not a quieter one just because it's a different kind of content.

**Resampling happens at playback time, never at load/record time - a
correction to an earlier draft of this plan, caught by the user.** The
original design resampled a loaded file once, eagerly, right after
downmixing, on the theory that "every later part of this plan already
assumes 1 buffer frame = 1/output-rate seconds." That's true, but doing
the resampling *there* is a real data-loss bug, not just an
implementation detail: `SampleContent::getBuffer()` would then hold only
the *resampled* audio, with the true original discarded - so opening a
song recorded (or loaded) under one output rate (e.g. a 192kHz session),
then reopening and saving it under a *different* one (e.g. 48kHz) later,
would silently bake that lossy downsample into whatever gets written back
to the sidecar `.wav` (Part 7), permanently losing the original. The
project's own current output rate must never be allowed to leak into
what gets persisted for a clip whose audio was captured at a different
one.

The fix: `SampleContent` remembers the buffer's own true rate
(`getNativeSampleRate()`/`setNativeSampleRate()`, added to Part 3's data
model above) - `loadMonoSample()` reports the file's own native rate
without ever resampling it away (see its own updated signature above);
`Controller::startRecording()`'s captured take likewise gets tagged with
whatever `getAudioOutSampleRate()` was *at that moment* (Part 4/10), not
assumed to always match whatever the session happens to be running under
by the time the song is next saved. Resampling to match the rate actually
in use happens on demand, per trigger, in `SampleTrackState::
triggerClip()` (Part 5) - it builds a converted copy to play from
`getBuffer()` when `getNativeSampleRate()` doesn't match the current
`ChannelConfiguration::getAudioOutSampleRate()`, but never writes that
copy back into `SampleContent` itself. Not cached in v1 (a per-trigger
resample is cheap relative to how rarely a clip is actually (re-)triggered
- a real cache, keyed the same way Part 13's stretch cache already is, is
a plausible future optimization, not needed for correctness). Saving
(Part 7) always writes `getBuffer()` - the untouched original - tagged
with `getNativeSampleRate()`, never the session's current rate, so a save
under a different output rate than the clip was captured at can never
degrade what's on disk.

**Considered and rejected: using Part 13's SoundTouch for this instead of
a separate resampler.** Checked directly against SoundTouch's own
documentation rather than assumed: its "rate" control is explicitly
varispeed ("as if a vinyl disc was played at different RPM rate" - its
own docs) - deliberately changing pitch *and* speed together, the
opposite of what sample-rate conversion needs (identical audible content,
just re-expressed at a different sample count). `setSampleRate()` in its
real API tells the algorithm what rate a *fixed-rate* stream is at for
its own internal windowing math - it doesn't convert between two
different rates at all. So SoundTouch remains the right tool for Part
12's problem (tempo mismatch, pitch must be preserved while duration
changes) and the wrong tool for this one (rate mismatch, *nothing* should
audibly change) - genuinely different operations, not two views of the
same one, even though both now happen at the same conceptual point
(derive a corrected buffer from the original, on demand, at trigger time,
never mutating the stored source) and could reasonably be chained in one
pipeline function (resample first if needed, then time-stretch if
needed) once Part 13 exists.

A small resampler (`src/dsp/Resampler.h`, sitting alongside `RealFFT.h` as
another small, dependency-free DSP building block) - linear interpolation
is enough for v1 (simple, no new dependency, adequate for spoken/
percussive one-shots and loops); a higher-quality windowed-sinc resampler
is a plausible future upgrade (`third_party/pocketfft/` is already
vendored and could back one) but out of scope here, the same "not an
oversight, revisit if it becomes a real problem" framing Part 3 already
uses for other simplifications.

**Recording needs no resampling at all**, unlike loading - confirmed via
`AlsaAudio` (`AlsaAudio::AlsaAudio(int _freq, ...)`, `AlsaAudio.cpp`'s
`initialize_alsa_dev()`): playback and capture are opened against the
same single negotiated project rate, so `Controller::startRecording()`'s
captured frames are always already at whatever `getAudioOutSampleRate()`
is *at the moment of recording* - never a resampling problem in the
moment, only potentially later, if the *song* is reopened under a
different rate in a future session (handled above, uniformly with the
loaded-file case, via `getNativeSampleRate()`).

**Recording stays mono at the source.** `Controller::startRecording()`
already constructs a 1-channel `AudioBuffer` (`Controller.h:269-272`) - no
change needed there; Part 4 covers everything about turning a take into a
real `Clip`/`SampleContent`.

**Original tempo, per the user's own follow-up - needed for Part 13's
time-stretching, not used by anything before it. Lives on
`SampleContent`, not `Clip`.**
```cpp
short getOriginalTempo() const; // 0 = unknown/not set
void setOriginalTempo(short bpm);
```
The tempo this audio was actually captured/authored at. A live recording
(Part 4) sets it with certainty, to whatever `Song::getTempo()` is at the
exact moment the clip is created - never left to the artist to supply by
hand, since the app genuinely knows it. A file-referencing clip is the
opposite: per the user's own correction, loading a file is **XML-only in
this pass, no in-app command at all** (see Part 8 - the earlier idea of a
`load-sample` M-x command is cut; adding a `<clip><sample file="...">` by
hand, reusing Part 7's existing load path, is the only way to bring an
external file in for v1) - and because there's no app-side loader to
leave it "unknown" gracefully, `originalTempo` is *required* in practice
for such a clip: whoever hand-authors the entry supplies it directly, in
the very same edit, the same way they'd already need to know the file's
real duration/content to reference it meaningfully at all. `0` (omitted)
still parses and is tolerated defensively (Part 13 treats an unknown
origin as "don't guess, don't stretch" - the same "don't invent a number
you can't actually know" caution `Pattern::length_`'s own `0` convention
already uses elsewhere in this file) but is a hand-authoring omission to
avoid, not an expected steady state the way it is for other optional
fields here. Persisted alongside the `in`/`out` trim points
(`<sample originalTempo="...">`, Part 7). Read only by Part 13 (comparing
against the song's *current* tempo to decide whether this audio needs
stretching before its clip's next trigger) - nothing before that part
touches it.

**Duration -> row length.** Both a finished recording (Part 4) and a
loaded file need `Clip::length_` (still a `Clip`-level field - a clip's
row-window applies the same way regardless of what kind of content fills
it) set from real seconds, not authored by hand the way a Pattern clip's
length is - one small helper (`ChannelConfiguration::framesToRows(int
frames, int tempo)`, alongside its own existing `getSampleInterval()`)
shared by both call sites, rounding up so a clip's own window is never
shorter than its actual audio. Computed from the *full* buffer at
creation time (no trim points set yet - see below); if the artist trims a
clip afterward by hand-editing `in`/`out`, adjusting `length` to match is
their own call too, the same way editing any other authored field here
never auto-syncs a sibling one.

**In-point/out-point trimming, per the user's own decision - also on
`SampleContent`:** hand-editable in the XML, in seconds, non-destructive
(the underlying `AudioBuffer` - and its own sidecar `.wav` - is never
physically cut, only what plays is bounded). No automatic
silence-detection in this plan ("useful but not necessary," per the user)
and no in-app editing command either - purely an XML field for now,
consistent with how other hand-editable song data already works.
```cpp
float getInPoint() const;  // seconds trimmed off the start, default 0.0 (no trim)
float getOutPoint() const; // seconds trimmed off the end, default 0.0 (no trim)
void setInPoint(float seconds);
void setOutPoint(float seconds);
```
Symmetric by design: each field is "how much to cut from that end," not an
absolute timestamp, so `0.0` is a genuine, meaningful default for *both* -
no sentinel value needed the way an absolute out-point would have required
(a real duration would otherwise be indistinguishable from "not set").
This also stays correct if the underlying sample is later replaced by a
different-length recording without needing to re-derive anything, the same
robustness a sentinel was there for, just without needing one. Resolved to
actual frame indices only at the point playback needs them (Part 5's
`triggerClip()`): `in_frame = round(getInPoint() * output_rate)`,
`out_frame = total_frames - round(getOutPoint() * output_rate)`, against
the project's own *current* output sample rate
(`ChannelConfiguration::getAudioOutSampleRate()`) - correct regardless of
whether the buffer being played is `getBuffer()` directly or a
just-resampled-for-playback copy of it (above), since either way it's a
buffer already at the output rate by the time this math runs. Clamped
defensively there (`0 <= in_frame < out_frame <= total_frames`, falling
back to the full buffer on a degenerate hand-edit - trim amounts that
together exceed the buffer's own real duration, or either value negative)
rather than trusting the stored values outright.

## Part 4 - Recording a clip from a live audio source

This replaces Part 2's removed flow with one that actually starts, runs,
and ends.

**Naming, deliberately not "recording."** This codebase already has three
distinct, already-named things a user or a future reader could reasonably
call "recording": `PatternEditor::isAutoRecording()`/`LaunchpadManager::
isAutoRecording()` (live-hold real-time note entry into a Pattern as you
play), Session view's Record Arm/`capture_enabled_` (assign-clip-instance-
into-the-scene vs. trigger-live), and this part's own mic-capture-into-a-
`SampleTrack`-clip. `Controller`'s own existing method names
(`startRecording()`/`stopRecording()`/`isRecording()`) already happen to be
this third, audio-capture-specific meaning - kept as-is, they're not
ambiguous *among themselves*. What needs distinct naming is the new
user-facing M-x command pair and the new `Controller` method Part 4 adds -
`start-sample-capture`/`stop-sample-capture` and `Controller::
finishSampleCapture()` below, using "capture" rather than another
"recording" name, so none of the three existing concepts and this new one
share a word a command palette or a doc comment could confuse.

**There is no "finishing" - only starting and no-longer-appending. Not
eagerly, though - lazily, on the first real audio block, per the user's
own follow-up correction.** A take isn't built in a scratch buffer and
turned into a `Clip` only once it's over - `Controller::
beginSampleCapture()` creates the `Clip`, adds it to the track's own clip
list, and (when recording during playback) places it in the arrangement,
all as one step, well before the take ends. An earlier draft of this plan
had that step run synchronously at the very keypress that starts
recording - reconsidered: a clip created before any audio has actually
arrived has a 0-frame buffer, which shows nothing in Session view/
`ArrangementGrid`/Part 12's waveform either way, so creating it that
early bought no real visibility benefit, only the risk of leaving a
genuinely empty clip (and a placed-but-silent instance) behind if the
take ended up capturing nothing at all - which then needed its own
cleanup path. Creating it lazily, the *first time real audio actually
lands* (`UI::handleRecordEvent()`, below), gets the same "visible almost
immediately, not only once you press stop" property without ever being
able to end up empty - if no audio ever arrives, `beginSampleCapture()`
is simply never called, and there is nothing for `stop-sample-capture` to
clean up either. "Stop" is just "stop appending new audio, and finalize
what's there if anything is" - a creation event only conditionally, not
always.

**Start, targeting a `SampleTrack` automatically - no separate
`add-sample-track` step required.** A new `start-sample-capture` command
(M-x only for v1): if the cursor is already on a `SampleTrack`, records
into it; otherwise creates a fresh sibling `SampleTrack` right there (the
same `song.addTrack(make_unique<SampleTrack>(), current_track_id())` Part
8's own `add-sample-track` uses) and targets that instead - a performer
hitting "record" shouldn't need a separate track-creation step first.

**Also starts the transport, the same way every other recording flow in
this codebase already does - raised directly by the user.** Recording
without playback running would mean recording into silence with no
timeline position at all, and this codebase already has an established,
directly reusable pattern for exactly this from the existing note-entry
auto-record flow (`PatternEditor`/`LaunchpadManager`'s
`isAutoRecording()`): if the transport isn't already playing,
`Controller::startAutoRecordPlayback(auto_started_playback)`
(`Controller.cpp:847-851`) - already exactly the right shape for this,
*not* its sibling `startAutoRecordSession()`, which additionally mutes
the song's own pattern playback (`SET_RECORDING_MUTE`) so old notes don't
retrigger while new ones overwrite them - irrelevant here, since
sample-capture never touches Pattern data at all, and muting the existing
backing track while recording *over* it would defeat the entire point.
`auto_started_playback` is a new bool `start-sample-capture`/`stop-
sample-capture` share (a new `PatternEditor::sample_capture_auto_started_
playback_` member, mirroring `auto_started_playback_`'s own existing
shape exactly, deliberately its own field rather than reused - the
note-entry flow's own flag means something specific to *that* flow) -
false if playback was already running (the artist's own doing, left alone
on stop), true if this command is the one that started it (stopped again
on `stop-sample-capture`, below). Scene growth while a long take runs
past the end of a short scene reuses `extendRecordingSceneIfNeeded()`
(`Controller.cpp:820-835`) exactly as-is too - already generic over
"which input source is actually recording" by design (its own doc
comment: "a single shared model-level concern") - `UI.cpp:738`'s existing
union just grows one more term: `... || getController().isRecording()`.

`isRecording()` becoming true is already exactly what arms the capture
descriptors in `Player.cpp`'s poll loop (`Player.cpp:454`) - that
plumbing was always correct; only the far end (`UI::handleRecordEvent()`,
Part 2) was broken.

**While recording.** `UI::handleRecordEvent()` (`UI.cpp:838-846`) keeps its
`addToSample(ev.getData())` call (mono capture blocks appended to
`current_sample`, unchanged) but loses the `Note(1)` write (Part 2) -
and, right after, calls `Controller::beginSampleCapture(track_id)` if
`!hasRecordingClip()` yet (this take's very first real block - every
later call this take is a no-op via that same guard). Its `setStatus()`
call is worth extending to show live elapsed time (from
`getCurrentSample().numberOfFrames()` and the output sample rate) rather
than a raw frame count. Part 12's own waveform cache needs to tolerate
this growing-buffer case explicitly - see that part's own note.

**Stop.** A new `stop-sample-capture` command (or the same key toggling
start/stop - left to implementation) calls `Controller::
finishSampleCapture()`:
- If `hasRecordingClip()` is false (no audio ever actually arrived this
  take - no capture device available, or stopped again before a first
  block landed), there is nothing to do beyond the housekeeping below -
  `beginSampleCapture()` was never called, so no `Clip` exists to clean
  up either. The plain no-op this always was, restored by creating
  lazily instead of eagerly (above) rather than needing its own
  `deleteClip()`-based cleanup path.
- Otherwise: `clip.setLength(...)` (Part 3's duration-to-rows helper),
  computed now that the real duration is finally known - the one piece
  `beginSampleCapture()` couldn't set when the clip was first created.
- If `sample_capture_auto_started_playback_` (above) is true, stop the
  transport now that the take is done (`togglePlaying()`, mirroring
  `stopAutoRecordSession()`'s own playback-stopping half - but skipping
  its `SET_RECORDING_MUTE` toggle-off and edit-position-landing steps,
  neither relevant here, since sample-capture never set the mute or moved
  any pattern-edit cursor in the first place) and clear the flag;
  otherwise leave playback running exactly as the artist left it.
- `stopRecording()` to clear `current_sample`/`recording_track_id`, and
  report success via `setStatus()` (clip id/name + duration) - the same
  precedent the removed `"recorded {} frames"` message already set.

**Latency-compensated in-point trimming is a deliberate follow-up, not
built in this pass.** Part 11 (below) still describes the design for
measuring round-trip latency and baking it into a take's own in-point
before the clip is ever visible - genuinely needs a real ALSA-level
measurement only reachable from the audio thread (`AudioAPI::
getPlaybackDelayFrames()`/`getCaptureDelayFrames()`, `Player.cpp`), which
this pass doesn't wire up. What's built here - lazy lookup-by-first-real-
block clip creation, immediate placement at wherever the transport is -
is the foundation that follow-up slots into later: `beginSampleCapture()`
gains an in-point trim once the measurement exists, without needing to
change when or how the clip itself gets created.

**Scope, explicit.** Only one take can be in progress at a time, on
whichever track is currently armed - `Controller::current_sample`/
`getRecordingTrackId()` stay the single, global fields they already are
(`Controller.h:795`, `:821`); this plan does not add per-track concurrent
recording. No silence-detection or maximum-duration auto-stop in v1 -
a take runs until the performer explicitly stops it, an explicit,
accepted simplification.

## Part 5 - Playback: triggering a sample clip (transport-driven)

**`SampleTrackState : InstrumentTrackState`** (new, e.g. local to
`SampleTrack.cpp`'s anonymous namespace, same pattern as
`DrumMachineTrackState`/`PercussionTrackState`) - inherits `noteOn()`/
`stopVoices()`/`stopAllVoices()`/mute/solo/sends/position for free, the
same way those two do. `SampleTrack::createState()` returns one, mirroring
`DrumMachineTrack::createState()` (`DrumMachineTrack.cpp:56-59`).

**A small adapter "instrument."** Not resolved through `InstrumentPool`
(a `SampleTrack` has no `instrument_id_`, same reasoning
`DrumMachineTrackState`/`PercussionTrackState` already have for overriding
`getInstrumentSource()`) - instead, `SampleTrackState` gets a new
`triggerClip(const Clip & clip)` entry point that resolves `clip.getSample()`
plus Part 3's in/out points (converted to clamped frame indices there, not
earlier - see Part 3) and constructs the adapter instrument on the fly with
them, then calls `noteOn()` with it directly. The adapter itself is Part 1's
repurposed `FileInstrumentVoice` logic: click-safe start (now at the
resolved in-point frame, not always sample 0), `encodePosition()` for
spatial placement/gain, no pitch handling - plus two additions: an explicit
end frame (the resolved out-point, not always `samples_->size()`), and
wraparound looping (`sourceSamplePosition_` resets to the in-point frame,
not always 0, once it reaches the out-point) when `clip.isLooping()` is
true.

**Wiring into `SongState::renderBlock()`'s existing scheduling loop.** The
loop already resolves `resolveInstanceAt()` per scheduled track and already
detects a clip-index transition to call `stopAllVoices()` on any
`InstrumentTrackState*` (`SongState.h:200-208`) - that part needs no
change at all, since `SampleTrackState` inherits it. What needs a new
branch is the "what happens once we know a clip is active" part
(`SongState.h:210-223`, which currently always resolves `active_pattern =
&clip.getLeafPattern()` and later reads `Notes` from it): for
`TrackType::SAMPLE`, skip the `getLeafPattern()`/`getNotes()` path
entirely - there's no Pattern content to read - and instead, exactly on
the transition *into* a real `clip_index` (not on every row the clip stays
active), call `sample_track_state->triggerClip(clip)`. Everything after
that (the buffer running until it ends, or until the next transition's
`stopAllVoices()`) needs no further per-row involvement from
`renderBlock()`.

## Part 6 - Session view / Launchpad live triggering

`LaunchpadManager::fireClipStep()` (`LaunchpadManager.cpp:354-368`) fires
one row's worth of Pattern notes per step - wrong shape for a whole
recording. `triggerClipStep()` (`LaunchpadManager.cpp:1312-1400`) already
does everything else needed generically (quantized launch/join/swap/stop
against the shared grid boundary, one-shot expiry via `relative_step >=
length`) and only calls `fireClipStep()` once, at its very end
(`LaunchpadManager.cpp:1382`).

Work: branch that final call (and the identical "launch immediately, from
silence" case in `handleSessionPadEvent()`, `LaunchpadManager.cpp:1227`) on
track type. For `TrackType::SAMPLE`:
- Only act when `relative_step % length == 0` (covers the first launch and
  every loop repeat of a looping clip in one condition; the surrounding
  one-shot-expiry check already short-circuits a finished one-shot before
  reaching this point, unchanged).
- Push a new `PlaybackControlEvent::PLAY_SAMPLE_CLIP` (track_id,
  clip_index) instead of calling `fireClipStep()`.

`Player.cpp` gets a new case for it, mirroring the existing `PLAY_NOTE`
case shape (`Player.cpp:211-267`): resolve `song.getClips(track_id)
[clip_index]`, `dynamic_cast` the track's live state to `SampleTrackState*`,
and call the *same* `triggerClip()` Part 5's transport path uses - both
callers converge on one function, so playback behaves identically whether
a clip started because the transport passed a placed instance or because a
performer pressed a Session-view pad.

Everything else Session view already does is confirmed track-type-agnostic
(keyed on `clip_index`/`clips.size()`, never on Pattern content) and should
need no change - verify at implementation time rather than re-deriving:
pad coloring/`refresh()`, Record Arm's assign path (`placeClipInstance()`/
`placeStopInstance()`), Stop Clip (CC49) hold+column targeting, the whole
quantized launch/swap/stop bookkeeping in `handleSessionPadEvent()`.

## Part 7 - Persistence (XML + sidecar WAV)

`SampleTrack` currently has **zero** XML support (confirmed: `"sampleTrack"`
isn't in `Song.cpp`'s `createTrack()` dispatch table at all, unlike
`"drumMachineTrack"`). Clip persistence itself, though, already exists and
is not gated by track type: `<clips><trackClips track="..."><clip ...>
<pattern>...</pattern></clip></trackClips></clips>` round-trips through
`Song.cpp:536-554` (load) and `:659-689` (save) for any track's own
`Song::getClips()`. Once `"sampleTrack"` is a resolvable track, its clips
already flow through that exact same loop with no extra plumbing - what's
actually new is a `file` attribute alternative to `<pattern>` on one
`<clip>`, used only when it carries a sample.

Work:
- `Song.cpp::createTrack()`: add `if (name == "sampleTrack") return
  make_unique<SampleTrack>();`.
- `Clip::loadParameters()`/`storeParameters()` (`Clip.h:86-96`) gain a
  `file` attribute, present only when `hasSample()`, plus the `in`/`out`
  trim attributes from Part 3 (`output.set("in", getInPoint(), 0.0f)`/
  `output.set("out", getOutPoint(), 0.0f)` - both default to `0.0f` now
  that each is "seconds trimmed off that end," the same "omit at the
  default value" convention `loop`/`length` already use two lines above
  them, so an untrimmed clip's `<clip>` element stays exactly as terse as
  it is today). Meaningless (and not read/written) for a clip with no
  sample.
- `Song.cpp`'s clip writer (`:678-687`) and reader (`:543-552`): for a clip
  with `hasSample()` true, skip the `<pattern>` child entirely and write/
  read `file` instead - resolving through Part 3's `loadMonoSample()` on
  load, and a small `writeMonoSample(path, buffer)` (`sf_write_float`,
  same libsndfile API `main.cpp`'s `--render` already uses to write WAV
  output) on save. `loadMonoSample()` returning `nullptr` (missing/
  unreadable file) is fatal to the whole `Song::open()` call, not skipped
  over - see Part 3's own note on why a named, exact file reference is
  held to a stricter standard than the system's own best-effort instrument
  resolution elsewhere.

**Sidecar naming.** `Clip` ids are already globally unique across the
*whole* `Song`, not just one track's own list (`Song::
generateUniqueClipId()`, `Song.h:276-286` - `"clip" + n`, checked against
every track's clips) - the id alone names a file unambiguously, no need to
also encode `track_id`. Convention: `<song-stem>.samples/<clip-id>.wav`,
sibling to the song file - e.g. `songs/welcome.xml` writes a clip with id
`clip3` to `songs/welcome.samples/clip3.wav`. `<song-stem>` is the song's
own filename with its `.xml` extension stripped. This directory sits next
to the song, not under `data/` - `.gitignore` reserves `data/` for large,
non-authored system assets (SoundFonts/SOFA files), not a song's own
recorded content; whether `.samples/` directories themselves get committed
alongside `songs/*.xml` or gitignored is the user's call, worth raising
separately rather than assumed here. The directory is derived from the
`filename` parameter `Song::open()`/`Song::save()` already receive
(`Song.cpp:454`/`:620`) - never stored as an absolute path in the XML.

**Save As.** `Controller::saveSongAs()` (`Controller.cpp:402-406`)
currently just calls `Song::save(filename)` at the new path. Since the
sidecar directory is derived from the song's own filename, saving under a
new name needs its own `.samples/` directory - copy each referenced clip's
WAV into it at save time, rather than leaving the new file's `<clip
file="...">` pointing back across directories at the *old* song's own
`.samples/`. Simpler and self-contained (a saved song's own directory
always holds everything it references, matching how the song's XML
already fully describes itself) at the cost of duplicating audio on disk
on every Save As - accepted.

**Orphan cleanup happens at save time, not at `deleteClip()` itself -
corrected during implementation, per the user's own principle that
nothing on disk changes as a side effect of an edit, only at an explicit
save.** An earlier draft of this part had `deleteClip()`
(`ArrangementOps.h:31-39`) delete the sidecar file directly, the moment
the clip is removed in memory - wrong: a delete the artist doesn't go on
to save should leave disk untouched, exactly like every other in-memory
edit here already does (a cleared note isn't rewritten to the XML file
either, until "save-song"). `deleteClip()` stays purely in-memory, same
as before. `Song::save()` sweeps `<song-stem>.samples/` instead, once,
every save: any `.wav` whose name (minus extension) doesn't match a
sample clip's id anywhere in the song's current `clips_by_track_` is
deleted. Runs unconditionally (even when the song currently has zero
sample clips at all - a leftover `.samples/` directory from before still
needs sweeping); a missing directory is not an error, just nothing to
sweep.

**Forward-compatibility seam.** All file I/O stays behind
`loadMonoSample()`/`writeMonoSample()` (Part 3), precisely so the user's
own roadmap item (embedded/binary storage for recordings and cover art) is
a change to that one seam later, not to every call site that touches a
clip's audio.

## Part 8 - UI / editor integration

- `add-sample-track` (`PatternEditor.cpp:576-599`): drop the
  `startRecording()`/`setRecordingTrackId()` wiring - becomes a plain
  `song.addTrack(make_unique<SampleTrack>(), current_track_id())`, matching
  `add-instrument-track` exactly - no longer the only way to get a
  `SampleTrack` either: Part 4's `start-sample-capture` creates one
  automatically too, when the cursor isn't already on one.
- **No `load-sample` command, per the user's own correction - loading a
  file from disk is XML-only in this pass.** Bringing an external
  recording in means hand-authoring a `<clip file="..." originalTempo=
  "...">` entry (Part 7's existing `file`-attribute load path, Part 3's
  now-required `originalTempo` for such a clip) directly in the song's
  XML, the same hand-editable spirit the `in`/`out` trim points already
  have - no in-app reader/minibuffer prompt, no `loadMonoSample()` call
  site anywhere in `PatternEditor.cpp`. `loadMonoSample()` itself (Part 3)
  is still real, exercised code - just called from `Song::open()`
  (Part 7), never from a UI command.
- `has_meter` (`PatternEditor.cpp:2332`, currently `track->getType() !=
  TrackType::SAMPLE`, commented "SAMPLE is the one leaf type with no
  InstrumentTrackState behind it yet") flips to true once
  `SampleTrackState` exists - update the condition and its now-stale
  comment.
- The `SAMPLE` placeholder-column rendering (`PatternEditor.cpp` ~2660,
  already draws a colored block sized to `getTrackWidth()`) gets extended
  to show the same leading-row clip-digit + instance tint the ordinary
  note-column branch just above it already computes via
  `resolveReadTarget()` - reuses that resolution, adds no new one.
- `ArrangementGrid` and Session view's own pad coloring are expected to
  need no change (both already resolve purely from `clip_index`/
  `color_ordinal_`, never Pattern content) - confirm during implementation
  rather than assuming. Confirmed: `ArrangementGrid.cpp` has no Pattern-
  content dependency in its rendering at all, and Session view's own
  `session_colors` computation (`LaunchpadManager.cpp`) keys entirely off
  `resolveInstanceAt()`'s `clip_index` plus each track's own hue identity -
  neither needed any change.

## Part 9 - Tests

- Unit: `Clip` sample round-trip (`setSample()`/`getSample()`/
  `hasSample()`), mono downmix behavior of `loadMonoSample()` against a
  small fixture stereo WAV, the new resampler against a fixture recorded at
  a different rate than the test's own `ChannelConfiguration` (output
  frame count and rough pitch/duration both correct), the duration-to-rows
  helper, and `Controller::finishSampleCapture()`'s zero-frames-discarded
  edge case.
- `RenderTests.cpp`-style: a fixture song with a `SampleTrack` and one
  placed clip instance renders the expected non-silent audio at the
  expected sample offset - extends the existing `renderSongOffline()`-based
  pattern (pan symmetry / hard-pan isolation tests are the precedent to
  follow).
- Launchpad e2e (`tools/e2e/`): implemented as
  `launchpad_sampletrack_session_test.xml` (+ sidecar `.wav`) /
  `verify_launchpad_sampletrack_stopclip.py` - the SampleTrack twin of
  `verify_launchpad_stopclip.py`, reusing its `fake_launchpad_stopclip.c`
  simulator unchanged against a SampleTrack fixture, proving
  `fireOrTriggerClipStep()`'s SAMPLE branch reaches `Player.cpp` over the
  real ALSA + audio-thread path. Hits the identical known, pre-existing
  sandboxed-environment limitation `verify_launchpad_stopclip.py` itself
  already has (`docs/known_bugs.md`) - confirmed unrelated to SampleTrack
  by reproducing it against the last commit before this script existed.
- Save/load round-trip: a song with a recorded/loaded `SampleTrack` clip
  saves its sidecar `.wav` and `file` attribute, reloads to the same
  audio content; `deleteClip()` on a sample clip leaves its sidecar file
  alone (purely in-memory), which then disappears on the *next* save, not
  before - both implemented as real tests
  (`tests/SongTests.cpp:sample_clip_round_trips_through_save_and_load`/
  `deleting_a_sample_clip_only_removes_its_sidecar_file_on_next_save`).
- Trim points: `in`/`out` round-trip through XML (present when non-default,
  omitted when not); `triggerClip()` starts/ends playback at the resolved
  frames, not the buffer's own bounds, and loops back to the in-point, not
  frame 0, for a looping trimmed clip; a degenerate hand-edit (trim amounts
  that together exceed the buffer's own duration, or either negative)
  falls back to the full buffer rather than producing silence or reading
  out of bounds.
- Update existing call sites that construct `SampleTrack` with the old
  constructor signature for the new no-arg one: `tests/SongTests.cpp:688`,
  `tests/SongStructureTests.cpp:82,117` (currently
  `make_unique<SampleTrack>(nullptr)`).
- Recording-latency compensation (Part 11): `finishSampleCapture()`, given
  a fixed `recording_start_scene_`/`recording_start_row_` and a fake
  `latency_frames` (no real ALSA needed for this - it's plain arithmetic
  once the frame count is known), sets exactly the expected `in` seconds
  and places the instance at exactly the snapshotted row, computes
  `length` from the post-trim duration; a freeform take (no snapshot taken)
  gets neither. Can't unit-test `AlsaAudio::getPlaybackDelayFrames()`/
  `getCaptureDelayFrames()` themselves headlessly (real ALSA hardware) -
  that's the manual verification step below instead.

## Part 10 - Clip-based note recording (Launchpad/keyboard)

Raised directly by the user, placed deliberately *before* latency
compensation: "the latency issues obviously apply to this one too so
this needs to be available so we can solve all latency issues at the
same time." Not a `SampleTrack`-specific change - this generalizes the
"record into a real, reusable `Clip`, not directly into the scene"
pattern Part 4 just built for sample-capture to the *other* live-recording
path this codebase already has: `PatternEditor`/`LaunchpadManager`'s
existing realtime note-entry recording (`isAutoRecording()`/
`startAutoRecordSession()`/`ensureRowCleared()`).

**What changes.** Today, a held-note recording session writes notes
directly into whatever `resolveEditTarget()` (`ArrangementOps.h`)
resolves to at the recording position - which, since nothing is placed
there yet at the moment recording starts, is always the scene's own
*background* Pattern (`resolveEditTarget()`'s own fallback case). The
recorded notes become permanent, inline scene content with no identity of
their own - there's no single "this take" object to select, loop via
Session view, move to another position, or delete outright if it didn't
work out; undoing a bad take means manually erasing individual notes.

**The fix reuses `resolveEditTarget()` completely unchanged.** It already
does exactly the right thing once a real instance is placed at a
position ("the active clip's own leaf Pattern ... when
`resolveInstanceAt()` finds a real clip active there") - the only new
work is making sure a real `Clip` *is* placed there before/as recording
writes into it, instead of leaving the position to fall through to
background. Mirrors Part 4's own lazy-creation precedent exactly, for the
identical reason (a clip created before anything is actually recorded
into it is empty and shows nothing, so there's nothing gained by creating
it any earlier than the first real note): the recording session's own
start (`startAutoRecordSession()`/`startAutoRecordPlayback()`,
`Controller.cpp`) doesn't create anything by itself; the *first* note
write of the session (wherever that actually happens today -
`Controller::writeReleaseOff()`/`applyNotePressure()`/the equivalent
note-on write path, exact call sites to confirm at implementation time)
creates a fresh `Clip` for the target track, `Song::addClip()`s it, and
`placeClipInstance()`s it at the row the session started on - after
which every subsequent write in the same session already resolves to it
automatically through the unchanged `resolveEditTarget()` call every
write site already makes. No new "which Pattern do I write to" logic
anywhere - only "is there already a clip for this session, or do I need
to create one first," the same shape `Controller::beginSampleCapture()`/
`UI::handleRecordEvent()`'s `hasRecordingClip()` guard already
established.

**The clip's own length has to grow with the take, mirroring
`extendRecordingSceneIfNeeded()` - a sibling mechanism, not the same
one.** That existing function grows the *scene's* own length in bars as
a recording session runs past its current end, so playback has somewhere
to keep going; a session recording into a placed clip instance needs the
exact same growth applied to the *clip's own* `length` instead, or
`resolveInstanceAt()` would stop considering it "active" partway through
a long take (its one-shot-length window would end while the performer is
still playing), silently dropping back to the background Pattern
mid-take. Same trigger condition (near the end of the current window,
called from the same per-row-advance hook), different target (`Clip::
setLength()` instead of `Scene::setLengthBars()`).

**Why this has to land before Part 11 (latency), not after.** Both
sample-capture (Part 4) and note recording write into something a
performer is reacting to *in real time*, against a backing track they
hear late (output latency) - and now, with this part built, both write
into a `Clip`/placed instance that can be individually corrected, rather
than one being a correctable unit (a sample clip's own in-point) and the
other being permanent, uneditable scene content with no equivalent knob
at all. Building this first means Part 11's actual latency work has a
real target to correct on both paths, and can measure/reason about both
kinds of correction together instead of building the mechanism once for
audio and rediscovering the identical problem for notes afterward.

**The correction itself differs in kind between the two, though - worth
being precise about, not conflating them just because the motivation is
shared.** Sample-capture's correction is continuous: trim a measured
number of *frames* off the front via `Clip`'s own in-point (Part 3),
needing both the output delay (what the performer heard late) and the
input/capture delay (getting their response back out of the audio
interface). A note's correction is discrete: there is no "trim the
front" for a `Note` sitting at a specific pattern row - the correction
instead has to shift *which row* a recorded note actually gets written
to, moving it earlier by however many rows the measured output delay
(alone - a Launchpad/keyboard press has no comparable capture-buffering
delay of its own; it's read essentially immediately, unlike audio through
an ADC) works out to at the song's own current tempo. Both draw on the
same underlying measurement (`AudioAPI::getPlaybackDelayFrames()`, Part
11) - notes just never need the capture half of it, and apply the result
as a row offset instead of a continuous trim.

**Scope, explicit.** This part is about *where* recorded notes land (a
placed `Clip` instance instead of the scene's own background Pattern) and
keeping that instance's own window growing with the take - it does not
by itself add the row-shift latency correction described above; that's
Part 11's own job, once this exists for it to act on. Session-view
looping/reuse of a note-recorded clip needs no new work at all once it's
a real `Clip` - it's already exactly the same kind of object an
`InstrumentTrack`'s own pooled clips are, launched the same way.

## Part 11 - Recording-latency compensation

Raised directly by the user: recording (Part 4) while the song plays back
(vocals over a backing track) has real round-trip latency - output
buffering delays what the performer hears, input buffering delays when
their captured response reaches the app - confirmed nowhere measured or
compensated anywhere in the current audio code (`AlsaAudio.cpp` tunes
period size low for live-note responsiveness but never queries actual
buffered-frame counts). Left unaddressed, a take recorded during playback
lands audibly late.

**The mechanism, per the user's own correction to an earlier draft of this
part:** don't compute where to place the clip - place it immediately, at
record-start, at wherever the transport genuinely is; that position is
correct by definition, nothing to compute. Compensate by trimming the
recorded audio's own front instead, using Part 3's in-point - the audio is
what's misaligned (relative to a fixed, already-correct placement), not
the placement.

**Why the misalignment is a *lead-in to trim*, not an earlier position to
shift to** - worth deriving once, since the direction is easy to get
backwards. Audio audible at real time `t` was rendered `output_delay`
earlier (it's been sitting in the output ring buffer since then), so what
the performer hears at `t` corresponds to a song position `output_delay`
*behind* the app's own live position tracking at `t`. A performer
responding to song position `P` therefore actually produces sound at real
time `t(P) + output_delay` (`t(P)` = the real-world moment the transport
was truly at `P`) - which then needs `input_delay` more before it's
readable back out of the capture buffer. Measured in frames from when
capture itself started (`t(R)`, `R` = the transport's row at record-start),
that content lands at buffer frame `(P - R) * sample_rate + latency_frames`,
`latency_frames` being the total round-trip delay. So the content meant
for the very first instant (`P = R`) doesn't appear until frame
`latency_frames`, not frame 0 - everything before that is lead-in the
performer could not possibly have produced any earlier, not misplaced
content to shift the clip back into. Trimming exactly `latency_frames` off
the front (`Clip::setInPoint()`) is what makes the post-trim audio's
effective start line up with the instance already sitting, unmoved, at
`R`.

**Placement, at start-time, unconditionally exact.** Per Part 4's own
"there is no finishing" correction, this now genuinely happens at
record-start, not merely snapshotted for later use: `start-sample-capture`,
when the transport is playing, reads `(scene, row)` from `Controller::
getPlaybackInfo()` synchronously, on the UI thread, right there - no
audio-thread involvement needed for this half, since `PlaybackInfo` is
already a plain UI-thread-readable snapshot - and places the just-created
clip there immediately (`ArrangementOps.h`'s `placeClipInstance()`).
`(scene, row)` is still kept on `Controller` afterward too (e.g.
`recording_start_scene_`/`recording_start_row_`, `-1` sentinel meaning
"not armed this take" - reset at the *start* of every take, not just read
once, so a freeform take started while stopped never accidentally
inherits a stale position from an earlier one) - not to place with later,
but so a subsequent in-point adjustment (below) has something to check
"was this take aligned at all" against.

**Measuring `latency_frames` needs the audio thread, and only that half
does.** New methods on `AudioAPI`/`AlsaAudio` (`AlsaAudio` is the only
`AudioAPI` implementation - confirmed, so this is a two-method addition,
not a fanout across other backends):
```cpp
virtual int getPlaybackDelayFrames() const = 0; // frames still queued before what's written next actually plays
virtual int getCaptureDelayFrames() const = 0;  // frames already captured but not yet delivered to the app
```
Implemented via `snd_pcm_delay(pcm_handle, &frames)`/`snd_pcm_delay(
capture_handle, &frames)` - the standard ALSA call for exactly this;
`latency_frames = getPlaybackDelayFrames() + getCaptureDelayFrames()`. Any
negative/failed result fails open to `0` rather than erroring - the same
defensive posture `AlsaAudio.cpp`'s existing XRUN recovery already uses
elsewhere in this file, and a missed measurement should degrade to "no
compensation" (Part 4's original, unmodified behavior), not break
recording. Measured exactly once per take: `Player.cpp`'s poll loop
(`Player.cpp:454`, `bool recording = controller_->isRecording();`) already
re-evaluates this every iteration to gate capture descriptors - it
currently just re-applies the same value unconditionally, so detecting the
false->true edge (a new local/member tracking the previous value) is new,
and is where this measurement happens, only when `controller_->
getPlaybackInfo().isPlaying()` is also true at that instant (matching the
"freeform take, nothing to compensate" case above).

**Crossing back to the UI thread respects the existing ownership
boundary.** `current_sample`/the recording-position fields above are
UI-thread-owned throughout this plan (`UI::handleRecordEvent()` is
already the sole writer of `current_sample`, never the audio thread
directly - confirmed by tracing `RecordEvent`'s existing flow). The
latency measurement, taken on the audio thread, follows the same rule: a
new small event (e.g. `RecordingLatencyEvent(int latency_frames)`) pushed
onto the existing `ui_event_queue`, handled by a new `UI::
handleRecordingLatencyEvent()` - no direct cross-thread field write.

**This is where the `Clip` actually gets created, trimmed, and placed -
not in `finishSampleCapture()`.** Per Part 4's own "never observably
applied after the fact" requirement, `UI::handleRecordingLatencyEvent()`
does the whole thing as one atomic step, only once, the first time a
latency measurement arrives for the take currently in progress (guarded
by, e.g., a `Controller::clipCreatedForCurrentTake()` check - a second,
spurious measurement for the same take should never happen given it's
only measured once per take, Part 11's own "measured exactly once per
take" note above, but this stays defensive rather than assuming it):
- `Clip clip(track_id)`; `clip.setSample(current_sample)` - the *same*
  `shared_ptr<AudioBuffer>` `startRecording()` returned back at
  record-start, not a copy, so it already carries whatever's accumulated
  since then and keeps growing live from here (Part 4's own "no
  finishing" note).
- `clip.setInPoint(getRecordingLatencyFrames() /
  (float)getChannelConfiguration().getAudioOutSampleRate())`.
- `clip.setOriginalTempo(song.getTempo())` (Part 3's new field - a live
  take's own tempo is always exactly whatever the song is running right
  now, known with certainty; see Part 13).
- A placeholder name (`"Take " + std::to_string(n)`); no length yet (0,
  "not given one" - Clip.h's own existing convention - the real,
  post-trim duration isn't known until the take actually stops).
- `Song::addClip(std::move(clip))`, remembering the new id
  (`Controller::recording_clip_id_`, alongside `recording_track_id`) so
  `finishSampleCapture()` can find this exact clip again by its stable id
  - never by list position, which could in principle shift if something
  else edits the track's clip list mid-take.
- Place it immediately: `ArrangementOps.h`'s `placeClipInstance(song,
  scene, track_id, recording_start_row_, clip_index)` at exactly the row
  snapshotted at record-start - the same function Session view's own
  Record Arm assign path already calls (`LaunchpadManager.cpp`), reused
  here rather than inventing a second placement mechanism. Re-validates
  `song.getScene(recording_start_scene_)` still resolves first (a long
  gap between start and this event landing is implausible, but stay
  defensive) - skips placement rather than placing into whatever now
  occupies that index if it doesn't.

`finishSampleCapture()` (Part 4's "Stop") then only has to finalize
`clip.setLength(...)` from the real, now-known post-trim duration
(`total_frames - latency_frames`, not the full buffer - the lead-in was
never real content, so it shouldn't inflate the instance's own
arrangement-window length either) - everything else about this clip
already happened here, at latency-arrival time. The genuinely-freeform
case (playback wasn't running at record-start - see Part 4's own note on
when this can still happen) skips all of the above entirely: no
`RecordingLatencyEvent` handling matters since there's nothing to align,
and `finishSampleCapture()`'s "Stop" is where such a clip gets created
instead, same as it always would without any latency compensation to
wait for.

**Scope.** No manual/user-configurable compensation constant - purely
`snd_pcm_delay()`-derived, per the user's choice. Flagged explicitly:
`snd_pcm_delay()`'s accuracy/availability can vary by driver (this
project's own "default" PCM in practice is PipeWire's ALSA compat layer -
`AlsaAudio.cpp`'s own period-size comment already notes this), so this
needs real-hardware verification, not just a headless test - see
Verification below.

## Part 12 - Waveform rendering in PatternEditor's clip boxes

Raised directly by the user: the `SAMPLE` placeholder-column block Part 8
already plans to extend with a leading-row clip-digit + instance tint
(`PatternEditor.cpp` ~2660) should draw the clip's actual waveform shape,
not just a flat colored block. Scoped to `PatternEditor`'s own per-row
rendering, per the user's own wording - not `ArrangementGrid`'s separate
bar-level clip blocks (`src/ui/ArrangementGrid.h`/`.cpp`), which draw
their own single colored block per bar rather than anything row-granular;
the same idea could extend there later (one clip's shape compressed across
however many bar-cells it spans) but that's a natural follow-on, not built
in this pass.

**Layout: time progresses downward, one row = one slice.** `PatternEditor`
already renders one Pattern row per screen line, and a placed sample-clip
instance already spans many consecutive rows (its own row-length, Part 3's
duration-to-rows helper) - so each row's own placeholder block (spanning
`getTrackWidth()` character cells, matching the existing placeholder's own
sizing) shows *that row's own slice* of the clip's waveform. Scrolling
down through the instance's rows reveals the whole shape top-to-bottom,
the same "the grid already is the timeline" layout every other row-driven
rendering in this file already uses - no separate mini-view or popup
needed. Exactly what each row's own block shows - not one flat bar - is
below.

**Glyph choice: block characters is the current lean, not yet finally
decided - braille stays a live option too, per the user.** Two real
character-cell candidates, both already precedented in this exact file,
neither picked yet:
- **Block Elements/sextants** (`TerminalUI.cpp`'s `TerminalHeatmapChart`):
  `kQuadrantCodepoints[16]` (classic Unicode Block Elements, U+2580-U+259F,
  a 4-bit mask over a 2x2 sub-cell, always assumed supported) with
  `sextantCodepoint(mask)` (Unicode 13 Symbols for Legacy Computing,
  U+1FB00+, a 6-bit mask over a 2-column x 3-row sub-cell) as the
  higher-resolution option where `notcurses_cansextant(
  ncplane_notcurses_const(...))` confirms real terminal support - a real
  runtime capability check, not a guess, exactly matching the user's own
  "check from notcurses what are supported" instruction.
- **Braille** (`PatternEditor.cpp`'s own `draw_vu_meter()`, `kGlyphs[]` at
  `PatternEditor.cpp:2129-2134`): the U+2800 block gives a 2-column x
  4-row (8-dot) sub-cell per character - actually *finer* than sextants'
  6 - and needs no capability check at all (a much older, near-universally
  supported Unicode block, which is presumably why `draw_vu_meter()`
  already uses it unconditionally).

Whichever wins, it's reused directly from its existing table/check, not
reimplemented a third time - a new mask-construction function ("which
sub-cells are lit" from amplitude, in place of the heatmap's brightness
threshold or the VU meter's single intensity level) is the only new code
either way. Left open for a quick side-by-side at implementation time
rather than decided here.

**Pixel graphics (sixel/Kitty) - explicitly a future upgrade, not v1.**
Per the user: character-cell glyphs (whichever wins above) are the actual
target now; `TerminalUI.cpp` already has the matching precedent for
adding a pixel-graphics-capable sibling later (`TerminalPixelHeatmapChart`/
`TerminalPixelChart`, both selected over their character-cell counterpart
via `notcurses_check_pixel_support()` when the terminal negotiates real
pixel graphics) - a real pixel-rendered waveform would follow the exact
same factory-selection pattern if/when it's built, not a new one. Flagged
directly by the user, though: neither existing pixel-graphics chart has
ever actually worked in Kitty despite the Kitty graphics protocol being
one of the two `notcurses_check_pixel_support()` can negotiate (sixel
being the other) - an open, unexplained gap in the existing precedent this
plan inherits rather than fixes; a future pixel-graphics waveform would
hit the same unresolved issue, not a new one of its own. Worth its own
`docs/known_bugs.md` entry independent of this plan.

**"Multiple values per row," per the user's own correction:** not one
flat amplitude bar per row - each character cell across the block's own
width (`getTrackWidth()` cells) gets its *own* 2 (quadrant) or up to 6
(sextant, 2 sub-columns x 3 sub-rows) independent amplitude sub-samples,
each sub-column's lit-sub-row-count (bottom-up, like a tiny bar-graph
column) standing in for that instant's amplitude. A row's own placeholder
block - several character cells wide - therefore already shows a short
multi-sample waveform slice across its width on its own, not a single
uniform fill; scrolling down through an instance's rows still reveals the
full shape top-to-bottom (unchanged from the layout above), now at
genuinely higher time resolution per row too.

**Deriving each sub-sample.** Reuses exactly the row<->time conversion
this plan already establishes elsewhere (Part 3/5/10's "1 frame =
1/output-rate seconds"; `unwrapped_row` from `resolveReadTarget()`
already gives the right per-row offset within the instance, the same
value the leading-row clip-digit logic already resolves) - convert a
row's own time span to elapsed seconds via the song's own tempo (the
inverse of Part 3's duration-to-rows helper), subdivide it into however
many sub-columns this row's block width provides, and for each take the
peak (or RMS) absolute sample value from the clip's own *post-trim* audio
(bounded by `getInPoint()`/`getOutPoint()`, matching what's actually
audible - Part 3/5's own frame-clamping logic) over that narrower
sub-range.

**Caching: indexed by time/frame, not by row - deliberately, for future
tempo-stretch compatibility.** Per the user's own forward-looking note:
once this project gets live tempo adjustment, a row's own duration in
seconds changes, so a cache keyed by row index would silently go stale
the moment tempo changes - the fix then should be "stretch the row<->time
mapping," not "recompute every cached amplitude value." Building the
cache that way from the start costs nothing extra now: cache a
downsampled peak/RMS array at a fixed resolution over the clip's own
*audio* (frames/seconds), entirely independent of row/tempo, alongside
the `Clip` itself - computed lazily the first time it's needed,
invalidated only when `setSample()`/`setInPoint()`/`setOutPoint()`
actually changes the audio itself (never by a tempo change, now or in the
future). Every row's own sub-samples above are looked up by mapping row -> seconds
-> a position in this fixed, time-indexed cache at render time, not by
indexing the cache by row directly - so a future tempo change is just a
different row->seconds mapping function reading the same cache, exactly
the "stretch the audio [mapping], don't recalculate the values" principle
the user described.

**Normalized per clip, not to a fixed reference.** The cache also tracks
the clip's own peak amplitude across its whole range, and every sub-
sample is scaled against *that*, not an absolute `1.0`/full-scale
reference - a quiet recording (a soft-spoken vocal take, say) should
still show a readable, close-to-full-height shape rather than a nearly
flat line, matching how waveform displays conventionally auto-normalize
per clip rather than per absolute sample value.

**Coexists with the leading-row digit/tint (Part 8), doesn't replace it.**
The waveform fill is the block's own base rendering; the leading row's
clip-digit + instance-color tint (Part 8's own bullet) draws on top of its
own row's bar exactly as already planned, unchanged. Only rows where
`resolveReadTarget()` reports a real instance whose clip `hasSample()`
draw a waveform at all - the no-instance/explicit-stop background case
keeps whatever fallback glyph Part 8 already draws there, nothing to show
a shape for.

## Part 13 - Time-stretching for tempo changes

Raised directly by the user, following on from Part 3's `originalTempo`
field: "it is obvious now that it is needed." A `Clip`'s audio is a fixed
recording at a fixed real duration; the song's own tempo defines how many
*rows* that duration should occupy (Part 3's duration-to-rows helper) and,
separately, where a placed instance's own row-window falls. Those two
facts only stay consistent for as long as the song's tempo never moves
away from whatever it was when the clip was created - the moment it does
(the artist adjusts the song's own tempo after recording, or authors a
`<clip file="..." originalTempo="...">` at a tempo that doesn't match the
song's own), the clip's real audio content and its row-window duration
disagree, and simply changing the row<->frame mapping (as if it were
just another resampling problem) would also shift *pitch* - wrong for
"the same recording, just fit to a new tempo." Real time-stretching
(changing duration, not pitch) is the only correct fix.

**Where it happens: once, cached, keyed by the tempo ratio - never live,
per-block, during playback.** Same "expensive DSP runs once, not every
render() call" precedent Part 3's resampler and Part 12's waveform-peak
cache both already establish here. `SampleTrackState::triggerClip()`
(Part 5) is where the choice is made: if `clip.getOriginalTempo()` is `0`
(unknown - Part 3's own "don't guess" convention) or already equals
`song.getTempo()`, play `clip.getSample()` directly, unchanged, exactly
as Part 5 already does. Otherwise, resolve (building it lazily the first
time, from the *trimmed* region only - `getInPoint()`/`getOutPoint()`
bound the source range, so a stretched cache never covers audio that will
never actually play - and cached against `(clip identity, current tempo)`
so a later trigger at the same tempo reuses it instead of re-stretching)
a **stretched copy** of that range at ratio `song.getTempo() /
clip.getOriginalTempo()`, and play that instead. Invalidated whenever
either side of the ratio changes - the song's own tempo (there is only
one, shared song-wide - `Song::getTempo()` - so a tempo change
potentially invalidates every `SampleTrack` clip's cache at once, not a
per-clip event to track individually) or the clip's own `setSample()`/
`setInPoint()`/`setOutPoint()`/`setOriginalTempo()` (same invalidation
triggers Part 12's peak cache already needs, extended by one more:
`setOriginalTempo()`). Never persisted - the sidecar `.wav` (Part 7)
always holds the original, unstretched recording; a stretched buffer is a
derived, in-memory-only artifact, rebuilt after every load/tempo change
exactly the way Part 3's resampled buffer already is.

**Algorithm/library: SoundTouch, per the user's own suggestion - not a
hand-rolled WSOLA, and not this codebase's existing `GranularEngine`/
`GranularCloud`.** A pitch-shifted fallback was considered (a plain
resample-based interim, matching Part 3's own "linear interpolation for
v1, higher-quality resampler deferred" precedent) and explicitly
rejected, per the user: a `SampleTrack` clip is real recorded material -
often with a real, meaningful pitch (vocals, a harmonic instrument) - and
changing that pitch as a side effect of a tempo-only adjustment would make
it clash with the rest of the mix's own key, not merely sound
lower-fidelity the way linear-vs-sinc resampling does. That's a
correctness defect, not a quality shortfall, so there is no acceptable
degraded fallback here - either real pitch-preserving stretching ships, or
Part 13 stays unbuilt and a mismatched clip simply plays at its own
original tempo/duration (musically correct pitch, out of sync with the
song's row grid) rather than at the wrong pitch.

SoundTouch (`codeberg.org/soundtouch/soundtouch`) is the right tool for
this rather than a new hand-rolled implementation: a mature,
widely-deployed (Audacity, VLC, ...) open-source pitch-preserving
tempo/rate-change library, confirmed LGPL v2.1 (`COPYING.TXT` in its own
repo), and already packaged for Ubuntu (`libsoundtouch-dev`, real apt
package - matches this project's own stated preference for linking a
system package over vendoring a full library source tree, the same
precedent `libnotcurses-dev`/`libsndfile1-dev`/`libmysofa-dev` already
set in the Run section's own Dependencies list, which gains
`libsoundtouch-dev` alongside them). `dsp/GranularEngine.h`
(`bus/GranularCloud.cpp`'s own send-bus effect) already proves this
codebase has windowed-overlap DSP conventions, but isn't reusable here
regardless of library choice - it's a *generative* grain scatterer
(randomized onset/duration/pitch per grain, built for a diffuse cloud
texture), not a content-preserving stretcher; applied to a vocal take it
would turn speech to mush, not gently slow it down.

Integration: a small wrapper (e.g. `src/audio/TimeStretcher.h`/`.cpp`,
alongside `SampleFileLoader.h`/`.cpp` - external-library-facing code, not
`dsp/`'s own "dependency-free" building blocks) around `soundtouch::
SoundTouch`'s `setTempo()`/`putSamples()`/`receiveSamples()` API,
producing the cached stretched buffer `triggerClip()` reads. Licensing:
LGPL v2.1 needs its own `THIRD_PARTY_LICENSES.md` entry (this project's
existing canonical list of vendored/linked obligations - `--licenses`
already prints it) alongside PocketFFT/tinyxml2's, even though this is a
linked system library rather than vendored source - dynamic linking
(matching how `notcurses`/`sndfile`/`fmt` are already linked, not
statically bundled) keeps LGPL's own compliance burden to "ship the
license text and permit relinking," already satisfied by how this project
distributes today.

**Scope, explicit.** A tempo change re-stretches a clip's cache for its
*next* trigger, not retroactively adjusting a voice already mid-playback -
the same "resampling happens once, not per-block" simplification Part 3
already accepts, extended here. No live, continuously-ramping tempo
automation support - a discrete tempo *value* is what a `Clip`/`Song`
compares against, not a curve.

## Part 14 - Loudness-threshold-triggered recording start (undecided between three designs)

Raised directly by the user. **Not yet decided which of three candidate
designs to build - the user has explicitly said so, after two rounds of
back-and-forth on this part already produced two different single-committed
drafts (each superseded, kept below as Option 2/Option 3 rather than
discarded, since both are real, viable candidates, not mistakes).
Implementation should not start on this part until one is actually chosen
(or the user asks for more than one, e.g. as a per-take/song setting -
not assumed here, since it hasn't been asked for).**

**Shared motivation.** Pressing Record Arm on a SampleTrack from the
Launchpad currently just arms the existing `start-sample-capture`-style
flow (Part 4) - transport auto-starts if needed, and the clip's own audio
begins exactly whenever mic input first actually arrives, front-trimmed by
hand afterward if there's unwanted lead-in silence. All three options below
are about improving on that "hand-trim afterward" step for a *sharp
attack* specifically - a transient's own rising edge is below any loudness
threshold for at least part of its own rise, so triggering capture only
once loudness crosses a threshold would truncate the attack, not just
leave silence in front of it; Options 2 and 3 fix this with a small,
fixed-length pre-roll ring buffer that's already listening before the
threshold trips, splicing its own already-captured content onto the clip's
front. Option 1 doesn't fix it at all - trimming lead-in silence and
recovering a clipped attack are different problems, and Option 1
deliberately only ever has the first one.

### Option 1 - Arm starts everything immediately; no ring buffer, no threshold

The simplest of the three, and nearly what already exists: Record Arm
(CC19) on a SampleTrack starts the transport (if needed) and starts
recording, both immediately, the same way `start-sample-capture` already
does today (Part 4) - the only new work is wiring Launchpad's CC19 to that
same flow when the currently-followed track is a `SampleTrack`, instead of
its existing `capture_enabled_` toggle (see the CC19 bullet in the shared
mechanism section below - identical regardless of which option is chosen).
Whatever silence
sits between "Record Arm pressed" and "performer actually starts playing"
becomes real, captured lead-in content in the clip's own buffer - trimmed
off afterward by hand via the already-existing `in`/`out` points (Part 3),
same as any other recording. No ring buffer, no threshold constant to
guess at, no new event type, no transport/placement interaction to reason
about at all.

**Trade-off.** Simplest by a wide margin - almost no new mechanism. But a
fast attack (a clap, a drum hit) played right at the start of a take can
still lose its very own onset if the performer's reaction time to "the
transport just started" is comparable to how fast the attack itself rises
- there is no recovery for that, only trimming *silence*, never recovering
a truncated transient. Best fit for a workflow where a moment of lead-in
(even a deliberate count-in) is normal and the take is trimmed by hand
afterward anyway.

### Option 2 - Transport starts on Arm; the clip starts on threshold, backdated

Transport starts for real, immediately, on Record Arm (same
`startAutoRecordPlayback()` `start-sample-capture` already uses) and keeps
running completely normally from then on - never paused, never adjusted.
While armed and not yet triggered, mic input is continuously buffered into
a fixed-length ring buffer (below). Once loudness crosses a threshold, the
clip is created with the ring buffer's own content spliced onto its front
(the recovered attack) - and because the transport may have already been
running for a while by the time that happens (the performer can wait as
long as they like before playing), the *clip's own placement row* is
backdated by the ring buffer's own span so the clip's audio and its
placement agree, the same class of fix Part 11's own `in_point` trim
represents for a different kind of misalignment, just applied to placement
here instead of trim. Bounded and sound specifically *because* the ring
buffer's own length is fixed and small - unlike "however long the
performer waited," which isn't, backdating the placement by that fixed cap
is a small, deterministic adjustment, not an open-ended rewrite of what
already, genuinely played on every other track in the meantime.

**Trade-off.** The transport - and every other track - keeps running
completely normally throughout the wait, which is exactly what's wanted
for the scenario Part 11 itself is about: recording a take (vocals, an
overdub) against an *already-playing* backing track, where the performer
comes in whenever they're ready and only their own new clip's placement
needs adjusting, not the whole song. Doesn't suit "the whole take should
start exactly when I play something" (a live count-in triggering
everything) - for that, see Option 3.

### Option 3 - Everything starts on threshold; the transport itself is backdated

Transport stays genuinely stopped through the whole arm-to-attack wait -
nothing plays, on any track. The moment loudness crosses the threshold,
*both* the transport and the recording start together, at that same real
instant - and because nothing was running on any track before that instant
either, there is no earlier interval during which something else "should
have" played and didn't - unlike an unqualified "fake the transport's own
start" while other tracks *were* already running, which would not be
sound, this is. The transport's own position *counter*
starts already advanced by the ring buffer's own span ("fast forwarded",
not paused-then-released) so the newly-placed clip, its own reported
length, and every *other* track's own scheduling all agree on how far into
this take the song genuinely is - `beginSampleCapture()` (Part 4) needs no
placement-row change at all here, unlike Option 2, since "wherever the
transport is right now" is already correct once its own counter has been
advanced.

**Trade-off.** Best fit for "the whole song/scene starts exactly when I
cue it" - a live count-in/first-hit-starts-everything workflow, where the
whole arrangement, not just the recorded clip, is time-locked to the true
attack. Does *not* support recording over an already-playing backing
track at all - by design, nothing plays until the trigger, so if the take
needs to line up with something else that's supposed to already be
sounding, Option 2 (or Option 1) is the right choice instead, not this one.
More moving parts than Option 2 (the transport's own start and position are
both touched, not just where one clip lands).

**Mechanism shared by Options 2 and 3** (Option 1 needs none of this):
- `dsp/RecordingRingBuffer.h` (new, alongside `dsp/SpectrumAnalyzer.h`'s
  own "ring-buffer accumulation over live audio" precedent): a fixed-
  capacity mono float ring, sized in frames from a compiled duration
  constant at construction. `push(const AudioBuffer & block)` copies the
  block's own Main channel in, overwriting the oldest content once full.
  `validFrames()` reports how much has actually ever been pushed, capped
  at capacity (armed for less time than the buffer's own length still
  drains correctly, just shorter). `drain()` returns a freshly-built mono
  `AudioBuffer` holding exactly `validFrames()` samples in chronological
  order and clears the ring for the next arm cycle - "drain", not "peek":
  once triggered, this content is consumed exactly once. `reset()` clears
  `validFrames()` back to 0 without touching capacity, for the
  disarm-without-triggering edge case below.
- `Player` owns the ring buffer (audio-thread-only state - never touched
  from the UI thread directly, the same ownership boundary Part 11 already
  established for its own cross-thread concern). Its poll loop's capture-
  descriptor-enable condition (`Player.cpp`, currently `bool recording =
  controller_->isRecording();` gating `.events`) becomes `recording ||
  controller_->isThresholdArmed()` - capture actually has to run
  (`snd_pcm_start()`ed, polled) from arm-time onward, not just once
  `isRecording()` is already true, or there is nothing to buffer yet.
- `Player` tracks `isThresholdArmed()`'s own previous value each iteration
  - the same "detecting the false->true edge (a new local/member tracking
  the previous value)" idiom Part 11 already uses for its own latency
  measurement. False->true (a fresh arm): `ring_buffer_.reset()` (a
  disarm-without-triggering followed by a later re-arm must not leave
  stale, temporally-discontinuous audio from the *previous* cycle sitting
  in the ring for a later `drain()` to splice in alongside genuinely fresh
  content). True->false without ever having triggered (an ordinary disarm)
  needs nothing further - the ring's now-orphaned content is simply
  overwritten by the next cycle's own reset.
- Each captured block, while armed and not yet triggered:
  `ring_buffer_.push(data)`, and `data.calculateMainRMS()` compared (as
  linear gain) against `TreeNode::decibelsToGain(kThresholdRecordTriggerDB)`.
  On the first block that crosses it: `ring_buffer_.drain()`, push a new
  `ThresholdRecordingTriggeredEvent(track_id, drained)` onto
  `ui_event_queue` (new event, `playback/`, alongside `RecordEvent.h`'s own
  shape - carries `track_id` and the drained pre-roll `AudioBuffer`), and
  latch a local "already triggered this arm cycle" flag, cleared on the
  next false->true edge - needed for the same reason the edge-tracking
  itself is: `controller_->isThresholdArmed()` won't actually flip false
  until the UI thread processes the event a few iterations later, and
  without this latch every intervening block still above threshold would
  fire its own duplicate trigger.
- `UI::handleThresholdRecordingTriggeredEvent()`: `controller.startRecording()`
  (flips `isRecording()` true, same as `start-sample-capture` already does
  synchronously today - just triggered here instead);
  `controller.addToSample(preroll)` (prepends the ring buffer's own content
  - the *same* accumulation path `UI::handleRecordEvent()` already uses for
  every later block, reused unchanged); `controller.beginSampleCapture(track_id)`;
  `controller.clearThresholdArmed()`. From here on this take is
  indistinguishable from an ordinary one - every further block arrives as a
  plain `RecordEvent`, handled by the existing, unmodified
  `UI::handleRecordEvent()` path.
- `Controller::armThresholdRecording(int track_id)`/`disarmThresholdRecording()`/
  `isThresholdArmed()`/`clearThresholdArmed()` - `armThresholdRecording()`
  sets `recording_track_id` (the same field `setRecordingTrackId()` already
  maintains) and flips the armed flag; **Option 2 additionally** starts the
  transport for real here too (`startAutoRecordPlayback()`, needing its own
  auto-started-playback bool the same way `PatternEditor::
  sample_capture_auto_started_playback_` already does, on whichever class
  ends up owning this call); **Option 3** does not touch the transport here
  at all. `disarmThresholdRecording()` clears the armed flag and, in
  **Option 2** only, stops the transport if this arm cycle itself started
  it (mirroring `stop-sample-capture`) - in **Option 3** there's nothing to
  stop, since arming alone never started anything.
- `LaunchpadManager::handleRawButton()`'s existing CC19 branch gains a
  track-type check, identical regardless of which option is chosen: when
  the currently-followed track resolves to a `SampleTrack`, each CC19 press
  alternately toggles `armThresholdRecording()`/`disarmThresholdRecording()`
  instead of the existing `capture_enabled_ = !capture_enabled_` toggle -
  the same alternating-press shape that toggle already has
  (`handleRawButton()` is only ever called on press - `UI.cpp`'s own call
  site already filters out release before reaching it), and the same "a
  different track type gives this control a different meaning" pattern
  `fireOrTriggerClipStep()` already established for Session-view triggering
  itself. `handleRawButton()` doesn't currently receive the cursor track at
  all - it needs a new `fallback_track_index` parameter, threaded through
  from `UI.cpp`'s own call site the same way its CC98 handling just above
  it already resolves one (`pattern_editor_->getCursorTrackIndex()` ->
  `resolveTrackId()`). Every other track type's CC19 behavior is untouched.

**Option 3's own extra piece: fast-forwarding the transport itself,
audio-thread-side, no event round-trip needed for this half** (the
ownership boundary that *does* require an event is `Controller`'s own
`current_sample`/recording bookkeeping above, never the transport itself -
`Player` already owns `live_states_`/`stateFor()` directly). On the
threshold-crossing block, before draining the ring buffer:
- `stateFor(buffer_name, song).setIsPlaying(true)` (plus whatever else an
  ordinary `PLAY` already does for the buffer it targets - demoting a
  different previously-playing buffer, `resyncPlayheadAfterStop()` - reused
  as-is, not reimplemented).
- Immediately advances that same state's position by the about-to-be-
  drained ring buffer's own frame count, converted to rows the same way
  Part 11 already converts a frame count to a row delta
  (`ChannelConfiguration::getSampleInterval(tempo)`):
  `state.setPosition(state.getAbsolutePosition() + preroll_rows)` - the
  same `setPosition()` `SET_POSITION`/`MOVE_POSITION`'s own handler already
  calls, just invoked directly here instead of arriving as an event, since
  this code already *is* the audio thread. A stopped buffer's position is
  otherwise still wherever it last was (0, for a take that never played
  this session), so this is what actually advances it.
- An explicit `pushSnapshots()` call for this one buffer, *before* pushing
  `ThresholdRecordingTriggeredEvent` - both go through the same single
  `ui_event_queue` FIFO, so this guarantees the UI thread's own
  `PlaybackInfo` mirror already reflects the fast-forwarded position by the
  time it processes that event and (inside `beginSampleCapture()`) reads
  `getPlaybackInfo()` for placement - without this, a stale
  pre-fast-forward snapshot could still be sitting in the UI thread's own
  mirror, landing the instance back at the *old* position. (This is also
  why `beginSampleCapture()` needs no signature change in Option 3, unlike
  Option 2's own placement-row backdate: by the time it runs, the position
  it reads is already correct.)
  This entire bullet is Option 3 only - Option 2's transport is never
  paused or repositioned, so it needs none of it.

**The live input level feeding the SampleTrack's own VU meter - applies to
whichever option is chosen, including Option 1** (armed-and-waiting only
matters for Options 2/3, but the meter gap during active *recording*, with
nothing playing back, exists in all three). `SampleTrackState`'s
`TrackInfo` (the same `isActive`/`isClipping`/RMS triple `PatternEditor`'s
meter already reads for every other track, via `TrackState::
getAllTrackInfo()` -> `Player::createPlaybackEvent()` -> `pushSnapshots()`,
all unmodified) is today only ever set from `renderVoices()`'s own rendered
output - silence whenever no `SampleClipVoice` is currently playing, which
is exactly the case while merely armed (Options 2/3) and the whole time
actual capture is happening in any option (nothing plays *back* while
recording). `SampleTrackState::setInputLoudness(float rms)` (new) is a
plain setter `Player` calls, same-thread, every block it actually captures
for this track, for as long as capture is happening at all (armed-and-
waiting in Options 2/3, or genuinely recording in any of the three) - no
new cross-thread field, since `Player` and this `SampleTrackState` both
already live on/are only ever touched from the audio thread. `render()`'s
own `TrackInfo` construction prefers this value over `renderVoices()`'s own
(silent) RMS whenever no voice is actually active, so the existing,
unmodified snapshot/meter pipeline just shows it.

**Scope, explicit.** `kRingBufferSeconds` and `kThresholdRecordTriggerDB`
(Options 2/3 only) are fixed compiled constants for v1, not exposed via any
command/knob yet - the same "no manual/user-configurable constant" scope
Part 11 already chose for its own comparable concern. Flagged explicitly,
though, as the more likely of the two to actually need exposing once this
ships and gets used for real: Part 11's constant is purely a
hardware-measured latency (nothing to tune, by definition), while a
loudness threshold is genuinely performance/source-dependent (a quiet
vocal vs. a loud drum hit) - a future per-song or per-take knob is a
plausible, low-risk follow-up, just not guessed at now. Needs the same
real-hardware, non-headless verification Part 11 already flags for itself
(ALSA capture timing), plus an actual microphone/audio-interface input
signal to cross a loudness threshold at all (Options 2/3) -
`RecordingRingBuffer`'s own push/`validFrames()`/`drain()`/`reset()`/
wrap-around behavior is the one piece of either genuinely headless and
ctest-coverable in isolation.

## Future direction (explicitly not needed yet) - merging a clip back
into the background

Raised directly by the user, explicitly flagged as not necessary now -
recorded here as a real, considered direction rather than a passing idea,
but deliberately not designed to the same level of rigor as Parts 1-13
above (the user hasn't converged on the details themselves - this is
still an open sketch, not a decision).

**The problem Part 10's clip-based recording introduces.** Once every
recording session becomes a real, reusable `Clip` (Part 10), a track's
own clip list can fill up - Session view shows exactly 8 rows per track
(`DrumMachineTrack::kMaxLanes`/the pad grid's own height, `CLAUDE.md`'s
own Session-view section). A performer who's just laying down ordinary,
never-to-be-reused scene content the classic tracker way, take after
take, would otherwise accumulate one clip per take with nothing to ever
launch or loop them for - exactly the clutter Part 10 was supposed to
avoid trading one problem for another.

**The sketch: a draft clip that promotes or decays.** A freshly recorded
take doesn't have to commit immediately to being a permanent, addressable
`Clip` (Part 10's own behavior) *or* permanent inline background content
(the pre-Part-10 behavior) - it could start as a third, provisional
state, a *draft*, that either:
- gets explicitly promoted to a real layer (today's Part 10 behavior -
  stays its own clip, addressable/loopable/deletable independently), or
- decays - either on an explicit "merge to background" action, or simply
  by not being promoted - and gets folded into the track's own background
  content instead, freeing the clip slot it would otherwise have
  occupied.

**Merging means something different per track type - deliberately not
symmetric underneath, even though the user wants the *capability* to
exist symmetrically.** For an `InstrumentTrack`/`DrumMachineTrack` clip,
"merging" note content into whatever's already in the background at the
same rows has no well-defined combination - two `Note`s can't be
"summed" the way two audio samples can, so the only sensible operation is
a destructive overwrite (the clip's own rows replace the background's, a
flatten rather than a mix) - trying to meaningfully "combine" two note
tracks is what the user calls "silly." For a `SampleTrack` clip, per the
user, merging is genuinely easy - two audio signals mix by straightforward
addition - and wanted mainly *for symmetry* with the note-track case,
not because `SampleTrack` needs it as urgently.

**The one real, buildable piece in this section, per the user's own
scoping: a `merge-clip-to-background` command (with a real keybinding,
not M-x-only) for a note-based (`InstrumentTrack`/`DrumMachineTrack`)
clip.** Everything else here is a sketch; this one is minimum-viable and
genuinely easy, precisely because the note case needs no mixing logic at
all - just the destructive-overwrite operation already described above,
run once, on demand, against whichever clip instance the cursor/focus is
currently on (the same "focused clip" `getFocusedClip()` other clip
commands already key off of): copy the clip's own `getLeafPattern()`
content into the scene's own background Pattern at the rows this
placement already covers (the same `row + getEffectiveRow(...)` mapping
playback itself already uses to read it, so the merge reproduces exactly
what was already audible, no surprises), then remove this one placement
(`ArrangementOps.h`'s `placeStopInstance()`) - not the clip itself, which
may still be placed/reused elsewhere and stays in the pool regardless.
No draft/decay lifecycle needed for this piece to be worth building on
its own.

**What a `SampleTrack` would need first: a background of its own - though
per the user's own clarification, a narrower need than it first looks.**
This capability doesn't exist yet and is a real prerequisite, not a
detail - today a `SampleTrack` has no continuous "background bed" the
way a scene's own inline Pattern already is for note tracks (this whole
plan's `SampleTrack` design is built entirely around discrete,
individually triggered clips - see Part 5/6). But it would need neither
its own trim points nor its own loop semantics, both already ruled out
directly by the user rather than left open: trimming happens on the
*draft* clip before it ever merges (Part 3's in/out points, already
resolved audio by the time anything reaches the background), so the
background itself never needs a second copy of that concept; looping
doesn't apply either, since the background is exactly as long as its own
scene, and scenes themselves don't loop (or if a future feature ever made
one, that would be the scene/song's own transport-level concern, not
something a `SampleTrack`'s background storage needs an opinion about).
What's actually still open is narrower: how the background audio itself
is stored and how it's summed with whatever the track's own placed clip
instances are sounding at the same time - a real design question, just a
smaller one than it first looked.

**Deliberately unresolved, left for whenever this becomes real work:**
the exact promotion/decay trigger (explicit command? a timeout? leaving
the clip's own row on a Launchpad?), what "decay" looks or sounds like to
the performer while it's pending, and the full shape of a `SampleTrack`'s
own background storage. Noted here so the clip-slot-scarcity problem it
solves is on record, not lost - not scoped or estimated as part of this
plan.

## Verification

1. `cmake --build build -j` clean, no new warnings (the build enables
   `-Werror=`/`-Wsign-conversion` - see CLAUDE.md's Conventions).
2. `ctest --test-dir build --output-on-failure` 100% pass.
3. `--render` a fixture song with a placed `SampleTrack` clip instance;
   confirm numerically non-silent output at the right offset, not just "it
   doesn't crash."
4. Manual: record a take from the mic (start, speak/play, stop), confirm
   it becomes a launchable Session-view clip on that track and survives a
   save/reload round-trip; load a stereo file from disk, confirm it
   downmixes to mono and plays back correctly positioned (azimuth/
   distance/extent) both from the Launchpad and from ordinary transport
   playback of a placed instance.
5. Manual: hand-edit a saved clip's `<clip in="..." out="...">` attributes
   to trim off leading/trailing silence, reload, confirm playback (both
   transport and Session-view triggering) starts/ends exactly where
   authored and that a looping trimmed clip loops within the trimmed
   window, not the whole recording.
6. Manual, real hardware (Part 11): start playback of a song with a
   clear, steady beat, record a take clapping/tapping in time with it,
   stop, and play the result back layered against the same backing track
   - confirm the recorded hits land on-beat, not audibly late; check the
   saved clip's `<clip in="...">` value is a small, plausible positive
   number (roughly the device's own known round-trip latency), not `0`
   (measurement silently failed) or something implausibly large.
7. Manual, real hardware, real mic (Part 14) - **not yet applicable: which
   of Options 1/2/3 to build hasn't been decided.** Once it is, this step
   needs to be filled in for that specific option (e.g. for Option 2 or 3:
   confirm the recovered attack's own rising edge is intact, not chopped
   flat, and that the clip's placement/the transport's own position agree
   with each other well enough that other tracks stay in sync; for Option
   3 specifically, that the transport genuinely never starts before the
   real attack; for any option, that the SampleTrack's own VU meter shows
   real input level while armed/recording, not just once a clip exists to
   play back). Sketched here so it isn't forgotten, not filled in yet.
