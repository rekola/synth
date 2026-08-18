# Per-buffer editing/playback state, and audition-while-playing

## Context

Multi-buffer support landed (`Controller::songs_`, the Buffers menu, `switchToBuffer()`/
`cycleBuffer()`/`killActiveBuffer()`, Emacs-style uniquify display names) — but only the
buffer *list* is per-buffer so far. Everything about *where you are* in a song — playhead
position, play/stop state, voice counts, the pattern-editor cursor/scroll/selection — is
still one process-wide set of variables. Switching buffers today doesn't save or restore
any of it: you land back on whatever position/cursor was last live, regardless of which
buffer that belonged to, and (per `Player.cpp`'s `SONG_CHANGED` handling) switching away
from a buffer that's currently *playing* tears its transport down entirely — there's no
way today to leave one buffer playing while looking at another.

Two related but separable pieces of work follow from that, below as Part A and Part B.
Part B was originally scoped as "can two buffers play at once" but narrowed after
discussion: **playing two full transports simultaneously isn't very useful**; what's
actually useful is **one buffer keeps playing while a different buffer is being edited,
and note-audition sounds from that second buffer (typing a note, clicking one in the
pattern to preview it) still play too**, mixed into the same shared ambisonic output.
That's a materially smaller feature than true N-buffer simultaneous playback, and Part B
below is scoped to exactly that.

Investigated against the actual code before writing this (not guessed): `SongState`
construction/reset cost, `Player.cpp`'s current `SONG_CHANGED` handling end to end, the
`Mixer`/`SendBusProcessor` relationship, and every call site that reads `getPlaybackInfo()`
or touches pattern-editor cursor/selection state. Findings are folded into the design
below rather than kept separate.

## Part A: per-buffer editing/playback state

### What needs to become per-buffer

On `Controller` (`src/Controller.h`):
- `PlaybackInfo playback_info` — position, pattern/row, is-playing, voice counts, meters.
- `int local_position_edit_seq_` — the race-guard counter paired with `playback_info`.
- `int recording_track_id` — which track is armed for input recording.
- `bool pattern_selection_active_` — mirrors `PatternEditor`'s own mark state.

Not per-buffer: `channel_config`/`mixer_type_`/`use_legacy_binaural_` (process-wide
device/decoder settings, unrelated to which song), `instrument_provider` (the shared SF2
catalog), the event queues, `commands_`/the IoC hooks, `current_sample` (an in-progress
mic-recording buffer — session-scratch, not song state), `pending_command_track_` (an
already-transient one-shot "Emacs prefix arg" style value, consumed immediately).
`last_saved_versions_` is *already* per-buffer, and is the pattern to copy: a
`std::map<std::string, T>` keyed by buffer name, parallel to `songs_`.

On `PatternEditor` (`src/ui/PatternEditor.h`):
- Cursor/scroll: `current_cursor`, `new_cursor`, `current_scroll_`.
- `edit_step_size`/`new_edit_step_size`, `current_song_version` (already naturally
  per-song — it's a version-diff baseline).
- Live-held-note bookkeeping: `active_midi_notes`, `active_keyboard_notes_`.
- The realtime auto-record session block: `auto_started_playback_`,
  `auto_record_cleared_rows_`, `last_cleared_row_`, `last_cleared_pattern_idx_`.
- Selection/mark: `selection_active_`, `selection_start_pattern_/row_/track_/col_`,
  `selection_start_scope_`, and the derived `current_sel_bounds_`.
- Annotation-editing bookkeeping: `annotation_screen_row_/col_`,
  `annotation_edit_row_/pattern_`.
- The cached last-rendered-playhead scalars (`current_score_playing_row/pattern/
  total_columns`) — derived from `playback_info` each frame, so they follow it.

Not per-buffer: `row_edited` (one-frame redraw-dirty flag, meaningless across a switch),
`clipboard_` (deliberately Emacs-style — a kill-ring is process-wide, you can yank into a
different buffer than you killed from; CLAUDE.md's own "check Emacs precedent" rule
applies directly here), anything geometry/style-related (not stored per-song at all — it
comes from `getDim()`/the `StyleProvider&` parameter each render call).

### Launchpad also needs to refresh on switch

Flagged separately because it's easy to miss the same way the Buffers-menu refresh was
missed earlier this session (a display surface driven off buffer/song state, with nothing
telling it to redraw when that state's *identity* changes rather than just its value):
`LaunchpadManager` reads `controller.getPlaybackInfo()` directly at several call sites and
takes a `playback_info` parameter into `refresh()` for step LEDs/active-voice LEDs/
audition-suppress logic — none of that currently has any notion that the buffer underneath
it can change. Two distinct problems, not one:

- **Redraw.** Whatever currently causes `LaunchpadManager::refresh()` to run (needs
  confirming against `LaunchpadManager.cpp` during implementation - not yet verified
  whether it's a per-frame poll or event-triggered) must also fire on a buffer switch, the
  same way `InfoLine`'s own per-frame dirty-check and the Buffers-menu's
  `setBufferChangeListener()` subscription do. Simplest fix is likely a second
  `setBufferChangeListener()` subscriber (or fanning the existing one out), matching the
  pattern already established for the menu.
- **Device-to-track associations don't survive a switch.** A Launchpad device's own
  "which track is this pad grid/fader following" state is stored as a `track_id` —
  `Track::getInternalId()` values are scoped to one specific `Song`, not portable across
  buffers. After switching to a different buffer, an association built against the old
  song's track IDs is either meaningless or (worse) silently valid-but-wrong if the new
  song happens to reuse the same numeric ID for a different track. At minimum, a buffer
  switch needs to clear/reset every device's track association rather than leave it
  pointing at a stale ID; whether to remember and restore each buffer's own prior
  associations (mirroring the pattern-editor cursor/selection treatment above) is a nice-
  to-have, not required for correctness.

### Design

Mirror `last_saved_versions_`'s existing shape: each of the fields above gets a
`std::map<std::string, T>` sibling (on `Controller` for the `Controller`-owned ones, on
`PatternEditor` for its own), keyed by buffer name. `switchToBuffer()`/`killActiveBuffer()`
save the *outgoing* active buffer's live scalars into its map slot and load the
*incoming* one's slot into the live scalars (default-constructing a fresh slot the first
time a buffer is switched to, matching a never-before-visited buffer showing row 0 with no
selection). `PatternEditor` needs the same save/restore hook, called from wherever it
already reacts to the buffer changing — likely via `Controller::setBufferChangeListener()`
(already wired for the Buffers menu; `PatternEditor` becomes a second listener, or the one
listener fans out to both) rather than a new bespoke signal.

`SongState` itself was confirmed cheap to keep N instances of — no header-only statics, no
process-wide caching (`Track::getState()` looks up/creates its `TrackState` child inside
whatever parent `TrackState` is passed to it, nothing cached on `Track` itself), so a
buffer's `SongState` can simply not exist yet until it's actually needed, with no
pre-warming or pool required (Part B settles the precise "needed" trigger, since it's the
part that actually owns `SongState` construction).

The ~25+ call sites that read `getPlaybackInfo()` (`PatternEditor.cpp`, `UI.cpp`,
`LaunchpadManager.cpp`) don't need to change *what* they call, only that the value they get
back now already reflects the active buffer correctly (handled once, centrally, in
`switchToBuffer()`) — this is the bulk of the "breadth" here, but it's read-site-transparent,
not a signature change at each site.

### Sequencing note

Part A is self-contained and buildable/useful entirely on its own, independent of Part B —
recommend doing it first. It also directly sets up Part B: once `playback_info` is
per-buffer, "which buffer is playing" naturally becomes "whichever buffer's own
`playback_info.isPlaying()` is true" rather than needing a new parallel concept.

### Effort

Small-to-medium. The mechanism to copy already exists in the codebase
(`last_saved_versions_`); the work is disciplined bookkeeping across the two save/restore
points and the field list above, not new architecture. Main risk is a forgotten call site
silently reading stale cross-buffer state (the exact class of bug already hit once with the
Buffers-menu refresh) — worth a couple of `ControllerTests.cpp`/manual-check cases
specifically switching buffers mid-edit and asserting cursor/position survive.

## Part B: one buffer plays while another is edited/auditioned

### Scope, explicitly

Not "two full transports running at once." At most one buffer is ever *playing*
(pattern-sequencer actively advancing) at a time — starting playback in buffer B stops
buffer A's transport if it was running, same as today. What's new: the buffer you're
*looking at/editing* (`active_buffer_name_`) no longer has to be the same one as the
buffer that's *playing*, and audition note-triggers (typing a note, clicking one in the
pattern to preview it — not the pattern-driven sequencer) against the active-but-not-playing
buffer still produce sound, mixed with the playing buffer's own output through the same
shared ambisonic `Mixer`.

That alone isn't quite enough, though: "stop buffer A's transport" can't mean "silence A
immediately." If A has a sustained/long-release instrument still sounding when B starts
playing, cutting it off outright would be an audible click/glitch, not a clean stop — and
if the user then switches again to C before A's tail (or B's, if B itself gets stopped in
turn) has actually finished ringing, there can legitimately be **three or more** buffers
with something left to render at once: A releasing, B releasing, C playing.

The number of buffers that ever actually make sound in one session is always small in
practice, though (a handful of songs, not hundreds - and per the laziness refinement in
"What has to change" below, an open-but-never-touched buffer never even gets this far) —
so rather than tracking each `SongState`'s lifecycle explicitly (is it playing/releasing/
audition-only right now, and pruning it once it goes silent), the simpler design is:
**once a buffer has ever made sound, it keeps a live `SongState` for as long as it stays
open, and every such buffer is rendered and accumulated into the shared `Mixer` every
block, unconditionally.** Only one thing distinguishes the "playing" buffer from the rest:
whether its pattern-sequencer is allowed to auto-advance and schedule new notes. Every
other live buffer's `SongState` has that muted (see below) but still renders whatever's
already sounding on it — so a release tail in A just keeps playing as part of A's own
still-live `SongState`, with no special-casing needed for the A→B→C case at all. The
trade-off, made explicit rather than hidden: every buffer that's ever made a sound this
session keeps running its own send bus (reverb/delay) every block regardless of whether
anything is actually audible there right now — already true for the one song that exists
today (`SendBusProcessor::process()` always runs, even on silent input, to keep its own
tail/feedback state continuous), just now multiplied by "how many buffers have ever
produced sound" instead of always being exactly one. Worth reconsidering if that count
turns out not to stay small in practice, but not before.

What would actually close this properly - a bus effect that tracks its own tail and
reports "I've decayed to silence, stop calling me" so a quiescent buffer's `SongState`
could drop out of the accumulate loop entirely, not just stay cheap - is explicitly **not**
part of this plan. A `SendBusProcessor` that never reports silence would mean it's
misconfigured (an infinite/non-decaying feedback setting isn't a musically sensible reverb
or delay to begin with), so this is a real, well-motivated future optimization, just an
independent one - it belongs to `bus/SendBusProcessor.h`/`BusEffect` and stands on its own
regardless of whether multi-buffer audition ever exists.

### What's already there to build on

- `Mixer::accumulate()` is already a multi-source accumulator with no notion of "who" is
  accumulating — `SongState::renderBlock()` already calls it once per top-level track plus
  once for the send-bus output, every block. `TrackState::renderChildren()` is the same
  "sum arbitrarily many independent renders into one shared accumulator" shape one level
  down. Nothing here needs new DSP.
- `SongState` has no singleton assumptions (Part A's own investigation) — keeping one
  alive per buffer that's actually made sound, not just one process-wide, is safe.
- The realtime auto-record feature already has a "mute the song's own pattern-driven
  scheduling, but keep rendering whatever voices are actually live" mode (used so a live
  take's manually-triggered notes are the only thing heard while auto-record holds the
  transport open) — this is *exactly* the mode every non-playing buffer's `SongState`
  needs, permanently (not toggled through some separate release/lifecycle state): never
  auto-advance its own pattern position, only react to explicit `PLAY_NOTE`/`STOP_NOTE`/
  `NOTE_PRESSURE` events (audition) or whatever voices it already has ringing. Reuse this
  rather than inventing a new mode.

### What has to change

1. **Decouple `mixer.reset()` from `SongState::renderBlock()`.** Today `renderBlock()`
   calls `mixer.reset()` as its own first line, so two calls back-to-back (one per live
   `SongState`) would have the second wipe out the first's output. Needs a variant (a bool
   parameter, or `Player` calling `mixer.reset()` itself once before looping over every
   live `SongState`) so `Player` can drive all of them into one shared `Mixer` per block.
2. **`Player` holds one live `SongState` per buffer that has actually produced sound, not
   one per open buffer, and not one total.** A buffer merely being open (in `songs_`)
   doesn't mean it needs a `SongState` - it's provably silent until something has actually
   triggered a voice in it, so there's nothing to construct yet. Ten songs opened from the
   command line at startup should mean ten `songs_` entries but (at most) one live
   `SongState`, not ten. The precise trigger is narrower than just "switched to as the
   active buffer": *being looked at/edited* still produces no sound by itself (no voices
   exist yet) - the real trigger is the first actual sound-producing event, i.e. the first
   `PLAY_NOTE` (audition or live editing) or the first time playback starts on it. This
   matters beyond just skipping idle `SongState` construction: some per-track effects (the
   tape-transport-style ones in particular) can generate real output from silent input once
   running (wow/flutter/noise floor modeling) - those shouldn't start running the moment a
   buffer is merely activated in the UI either, only once something's actually meant to be
   heard. (Simply switching to a buffer and reading/scrolling through it never has to run
   the DSP at all.) Once created, keep it alive continuously for as long as the buffer
   stays open (never torn down again just because it stops being active or playing - that's
   exactly what would reintroduce the release-tail-cutoff problem above); destroyed only
   when the buffer itself is killed (`killActiveBuffer()`/future kill-named-buffer). This
   is a refinement worth keeping in mind while implementing rather than a hard requirement
   of this plan - if it turns out a `SongState` is needed for some other reason the moment
   a buffer is merely activated (none identified so far - `PatternEditor` reads `Song`
   directly for display/editing, not `SongState`), falling back to "construct on
   activation" loses little. Each block, `Player` renders and accumulates every buffer that
   *has* a live entry (never all of `songs_`). Not a per-`SongState` mute flag each one
   carries independently, though - N independent booleans that all have to stay correctly
   synchronized is exactly the kind of thing that can drift (a missed update on some path
   leaves two buffers simultaneously unmuted, silently reintroducing "two transports
   playing at once," the one thing this whole design is meant to rule out structurally, not
   just by convention). Instead, `Player` holds a single `playing_buffer_name_` (mirroring
   `Controller::active_buffer_name_`'s own shape), and each block, only the `SongState`
   whose map key equals it gets its pattern-scheduler advanced - every other one renders
   audition-only regardless of any per-instance state. "At most one playing buffer" is then
   true by construction (there is exactly one name to compare against), not something that
   has to be kept consistent across N separate flags. The current `SONG_CHANGED` handling
   (`state_.clear(); state_.resetPosition(); state_.initialize(*song);` — a full rebuild of
   the *one* `state_`) needs to become "add one map entry lazily on that buffer's first
   sound, remove one on kill, reassign `playing_buffer_name_` on play/stop" - never a
   full-map rebuild just because the *active* (editing-focus) buffer changed.
3. **`PlaybackControlEvent` needs a buffer/song identifier.** Today it carries only a
   `Type` and four `int` parameters, and `Player::handlePlaybackControlEvent()` always
   resolves `controller_->getCurrentSong()` (the active buffer) and always operates on the
   single `state_` — there is currently no way for an event to say "this note is for
   buffer X specifically." Every producer (`Controller.cpp`, `PatternEditor.cpp`,
   `LaunchpadManager.cpp` — roughly 15 call sites total) needs to attach which buffer it
   means; audition events from `PatternEditor` mean "the active buffer," transport/
   sequencer-internal events mean "the playing buffer."
4. **Policy decisions to settle before/while implementing** (flagging now, not blocking the
   plan):
   - Killing the currently-*playing* buffer: stop its playback first, or refuse the kill?
     (Recommend: stop it automatically, consistent with how closing a document elsewhere
     usually means "and stop whatever it was doing.")
   - Does the active-but-not-playing buffer's audition still run through its own
     `SendBusProcessor` (reverb/delay), or bypass it? Recommend: run it through — it's
     already cheap per-instance (fixed-size ambisonic scratch buffers) and a dry audition
     preview would sound inconsistent with how the buffer actually plays.
   - Recording (mic input capture) targets whichever buffer is armed
     (`recording_track_id`, now per-buffer per Part A) — should probably always be the
     *active* buffer, independent of what's playing, but this needs a real decision once
     it's actually reachable (recording into a buffer that isn't playing is itself a
     separate, not-yet-designed feature).

### Effort

Medium — smaller than open-ended N-buffer simultaneous playback would have been (thanks to
reusing per-buffer `PlaybackInfo` from Part A and the existing mute-scheduling mechanism
from auto-record), but still a real change to the playback thread's core loop and the
event contract, not a localized fix. The `PlaybackControlEvent` buffer-id addition alone
touches ~15 call sites across three files.

## Suggested ordering

1. Part A on its own — immediately useful, self-contained, low risk.
2. Part B on top, once Part A's per-buffer `playback_info` exists to build "which buffer is
   playing" on.
