#ifndef _ARRANGEMENTOPS_H_
#define _ARRANGEMENTOPS_H_

#include "BarGrid.h"
#include <string>
#include <vector>

class Clip;

class Song;
class Pattern;
class ChannelConfiguration;

// Rounds `raw_row` up to the start of its own next bar (unchanged if
// already exactly on one) - every real-time placement that must not claim
// something had already started sounding before it actually did (Session-
// view's own clip-trigger/stop placement, LaunchpadManager.cpp) goes
// through this. Forward, not back: snapping backward would place an event
// as if it had taken effect from the start of a bar the performer hadn't
// actually reached yet, disagreeing with what they actually heard at the
// moment they pressed the pad.
int quantizedBarRow(int raw_row, int rows_per_bar);
// The same against a bar grid whose bars may change length.
int quantizedBarRow(const BarGrid & bars, int raw_row);

// The opposite direction, for a brand new clip's own placement
// (Controller::ensureNoteRecordingClip()) rather than triggering one that
// already exists: rounds `raw_row` down to the start of its own current
// bar. A live take's first note has to land *inside* whatever clip gets
// created for it, so the clip's own start can never be later than that
// note's own row the way rounding forward would risk - rounding back
// instead just gives the clip a few rows of leading rest before the
// performer's actual first note, same as any other pattern's content
// starting partway through it.
int previousBarRow(int raw_row, int rows_per_bar);
int previousBarRow(const BarGrid & bars, int raw_row);

// Places a real-clip instance event (clip_index - the clip's own ordinal
// position in track_id's own clip list, Song::getClips(track_id) - what
// actually gets stored is that clip's own stable id instead, Clip.h's own
// comment on why) at `row` on `track_id` in the arrangement. Clears away
// any other instance event already placed on that same track within the
// clip's first pass (row + its own length) first - a looping clip then
// plays on until the next event, never erasing later placements. A no-op
// if clip_index doesn't resolve to a real clip in that track's own list.
void placeClipInstance(Song & song, int track_id, int row, int clip_index);

// Places an explicit stop ("instantiate nothing") at `row` on `track_id`
// in the arrangement. Clears nothing, unlike placeClipInstance() above - a stop
// adds no new sounding content, so there's nothing of its own to protect
// going forward; whatever's already there from `row` onward was already
// left in a consistent state by whatever was placed before it. Callers
// that place a stop in response to a user action (ArrangementGrid's own
// Backspace) are expected to only call this where something is actually
// still sounding to begin with - a stop once placed silences every later
// row regardless of length or looping (resolveInstanceAt()'s own
// contract), so a second one placed further into that same already-silent
// stretch would do nothing a caller couldn't have skipped outright.
void placeStopInstance(Song & song, int track_id, int row);

// Merges the clip instance active at (track_id, row) into the track's own
// background content in the arrangement, across every row this one
// placement covers (a looping one up to the track's next event, or the
// arrangement's end), then removes that one placement (placeStopInstance() above) -
// not the clip itself, which may still be placed/reused elsewhere and
// stays in the pool regardless. Never calls Song::incVersion() itself,
// same as placeClipInstance()/placeStopInstance() above - the caller's
// job, gated on this function's own return value (true iff something was
// actually merged) so a no-op call doesn't bump the song version or claim
// success. A no-op if nothing real is placed at (track_id, row) (Arrangement::
// kStopInstance/kNoInstance).
//
// Not symmetric underneath, even though the capability is: a note-based
// clip merges as a destructive overwrite (two `Note`s have no well-
// defined "sum" the way two audio samples do, so the clip's own content
// at each row simply replaces whatever the background already had there -
// reproducing exactly what was already audible, not "combining" the two),
// while a SampleTrack clip merges as a real additive mix into the track's
// own background bed (Arrangement::getOrCreateSampleBackgroundContent()),
// resolved into real, materialized PCM the same way (resolveSampleAudio(),
// SampleTrack.h - baking needs actual samples to sum, unlike real-time
// playback's own resolveRealtimeSampleAudio()) and re-baked once per lap
// for a looping clip, matching what a listener actually would have heard.
// `channel_config` supplies
// the output sample rate/tempo-to-frames math the sample-mix path needs;
// unused by the note path.
bool mergeClipToBackground(Song & song, int track_id, int row, const ChannelConfiguration & channel_config);

// Resets `clip_index`'s own slot in track_id's own clip list
// (Song::getClips()) to a fresh, id-less filler (Song::ensureClipAt()'s
// own "hole" state) - first clearing away every instance event in the
// arrangement that referenced it
// (resolved by the clip's own stable id, same as placeClipInstance()'s
// own lookup). Never erases the element outright: holes are allowed, and
// every other track's own scene rows are indexed against this same
// list's own positions, so shifting everything past the deleted one down
// would silently misalign them all against it, even for a track this
// deletion never touched. A clip's own id is never reused (Song::
// generateUniqueClipId()), so nothing placed afterward could ever
// collide with a stale leftover reference the way reusing a freed vector
// position could. A no-op if clip_index doesn't resolve to a real clip
// in that track's own list.
//
// Purely in-memory, on principle - nothing on disk changes as a side
// effect of an edit, only ever at an explicit save. A deleted sample
// clip's own sidecar .wav becomes unreferenced, not deleted, here; the
// next Song::save() sweeps orphaned sidecar files itself (see its own
// comment), the same single moment every other in-memory edit here is
// already expected to wait for before touching disk at all.
void deleteClip(Song & song, int track_id, int clip_index);

// What deleting a slot removes, one layer per call: its clip, and once the
// slot is empty its stop button. NOTHING for a slot with neither (or one off
// the clip list). `deleted_clip_name` receives the removed clip's name.
enum class SlotDelete { NOTHING, CLIP, STOP_BUTTON };
SlotDelete deleteClipOrStopButton(Song & song, int track_id, int clip_index, std::string * deleted_clip_name = nullptr);

// Copies the clip at `from_index` into the slot below it on the same track
// (an independent copy under a fresh id, its content and name as they are
// now), overwriting whatever is there, and returns that slot, or -1 when
// there's nothing to copy. Placed instances of the source keep pointing at
// the source; those of an overwritten clip are removed.
int duplicateClip(Song & song, int track_id, int from_index);

// Places an independent copy of `clip` (a fresh id, retargeted to
// `track_id`) at `clip_index`, overwriting whatever is there, and returns
// that slot, or -1 when the clip is empty or the index negative.
int placeClipCopy(Song & song, int track_id, int clip_index, Clip clip);

// Snaps every note of the clip at `clip_index` to its closest row (a row is
// a sixteenth), using the sub-row delay a raw live take records, and clears
// that delay. A note-off moves with its note. A note landing on an occupied
// slot takes the next free column, an off the next row. A move past the end
// wraps in a looping clip and clamps in a one-shot. Commands are left alone.
// Returns false when there's nothing to quantize (empty slot, audio clip).
bool quantizeClip(Song & song, int track_id, int clip_index);

// The result of resolveInstanceAt() below. `start_row` is the resolved
// instance event's own row - only meaningful when `clip_index` is a real
// clip, but a caller needs it there: rendering that clip's own content
// at `row` means reading it at (row - start_row), wrapped by the clip's
// own length (Pattern::getEffectiveRow()), not at `row` itself.
struct ActiveInstance {
  int clip_index; // a real clip's own ordinal index, Arrangement::kStopInstance, or Arrangement::kNoInstance
  int start_row = 0;
};

// Resolves what's active at (track_id, row) in the arrangement - the same query
// real (transport) playback's own scheduler needs (SongState.h's own
// renderBlock(), hence this living in src/model/ rather than src/ui/
// alongside PatternBlockOps.h - real playback can't depend on anything
// UI-adjacent). Scans backward for the most recent instance event at or
// before `row`; that clip's own *current* position (resolved from its
// stored stable id, which never assumes it's still wherever it was when
// the instance was placed) if it's still active there (accounting for a
// one-shot's own native length having run out), Arrangement::kStopInstance if
// the most recent event is an explicit stop, or Arrangement::kNoInstance if
// nothing was ever placed on this track at or before `row` at all (or a
// stored id no longer resolves to any real clip).
ActiveInstance resolveInstanceAt(const Song & song, int track_id, int row);

// Bar-granularity counterpart to resolveInstanceAt(), for ArrangementGrid's
// own overview - one cell per bar, sampled at each bar's own first row.
// A short one-shot instance that both starts and (per its own length)
// finishes entirely inside a single bar's row span, never touching that
// bar's own first row, would otherwise never show up in any bar's plain
// resolveInstanceAt(raw_row) sample - not the bar it starts in (whose own
// first row is still earlier than the instance's own start), and not the
// next bar either (by then a one-shot short enough to fit inside one bar
// has already run out again). Resolves to whatever instance event is the
// most recent one at or before this bar's own *last* row instead; if that
// event's own row falls inside [bar_start_row, bar_start_row + bar_span)
// - it belongs to this bar - it's shown unconditionally, one-shot expiry
// included, since it was genuinely active for at least part of this bar
// regardless of what's true by the bar's end. Otherwise (the event
// predates this bar) falls back to plain resolveInstanceAt(bar_start_row),
// unchanged from before - a looping instance, or one whose own length
// still reaches this bar's first row, is unaffected by any of this.
ActiveInstance resolveInstanceForBar(const Song & song, int track_id, int bar_start_row, int bar_span);

// What editing (track_id, row) in the arrangement should actually read/write -
// the active clip's own leaf Pattern, live-linked to every other
// placement of it (editing through one instance updates them all), when
// resolveInstanceAt() finds a real clip active there; the track's own
// background Pattern otherwise (an explicit stop resolves the same way
// as no instance at all - a stop has no Pattern of its own to edit, only
// the background underneath it, currently inaudible only because of the
// stop event itself). `effective_row` mirrors exactly what real playback
// (SongState.h's own renderBlock()) reads at the same (track_id, row) -
// editing here always changes what's actually going to play.
//
// `focused_clip_id`, when non-empty, overrides all of the above: resolves
// directly against that clip's own leaf Pattern (looked up by id in
// track_id's own clip list - the same id-not-position lookup
// resolveInstanceAt() already does, for the same reason), row wrapped by
// the clip's own length, regardless of what's actually placed at `row` -
// editing a clip in isolation (Controller::getFocusedClip()), not
// "wherever it happens to be placed". Falls through to the ordinary
// position-based resolution above if the id doesn't resolve to a real
// clip on this track (stale/deleted - same resilience precedent
// resolveInstanceAt() already has for a dangling instance reference).
struct EditTarget {
  Pattern * pattern;
  int effective_row;
};
EditTarget resolveEditTarget(Song & song, int track_id, int row, const std::string & focused_clip_id = "");

// Read-only counterpart, for rendering - same resolution, but never
// creates a background Pattern entry as a side effect of merely reading
// (unlike Arrangement::getPatternsByTrack()[track_id], which would insert an
// empty one on every render pass for every track that's never actually
// been written to). `pattern` is never null - falls back to a shared,
// permanently-empty Pattern when there's truly nothing to read, the same
// "always something to hand back" sentinel convention Arrangement::getNotes()/
// getCommand() already use for the no-Pattern-at-all case.
struct ReadTarget {
  const Pattern * pattern;
  int effective_row; // wrapped by pattern's own length - what to actually read
  int unwrapped_row; // row - (background: 0, a clip: the instance's own start_row) - for a caller that wants to tell a pattern's own real rows apart from a shorter one's repeat (row >= pattern->getLength()), which effective_row can't answer on its own once it's already wrapped
  bool is_instance; // true when `pattern` is a real clip's own Pattern, false for the background - for a caller that wants to show the difference (e.g. PatternEditor's own instance-tinted rows)
  int clip_index; // only meaningful when is_instance - the clip's own ordinal position in track_id's own clip list (ArrangementGrid's own hex digit addresses the same index), for a caller that wants to show which clip this is, not just that one is active
  bool is_focused_override = false; // true when `focused_clip_id` (see resolveEditTarget()'s own comment) drove this resolution, rather than a real placed instance - lets a caller tell the two apart even though both set is_instance
  int repeat_length = 0; // rows after which the content repeats when greater than pattern->getLength() says (a looping clip whose Pattern has no length of its own); 0 = use the Pattern's
};
ReadTarget resolveReadTarget(const Song & song, int track_id, int row, const std::string & focused_clip_id = "");

// Which of `lane_values` are hit (a defined, sound-producing Note whose
// own getValue() matches) at `pattern`'s own `effective_row` - the step
// sequencer's shared by-value scan, generalized from PercussionTrack::
// getHitNotesAtRow() (which still exists, unchanged, for its own existing
// callers) so a pitched InstrumentTrack's own step grid (its lanes drawn
// from Song::getScaleDegrees() rather than a per-track lane list) can use
// the identical convention: a note left over from a lane no longer part
// of the current set (a removed drum lane, or a scale/key change moving
// which pitches are in scope) stays silently inert rather than firing or
// erroring, the same way PercussionTrack.h's own header comment already
// describes for its own lane list. Order matches `lane_values`' own
// order, not row/column order.
std::vector<int> getHitLaneValues(const Pattern & pattern, int effective_row, const std::vector<int> & lane_values);

#endif
