#ifndef _ARRANGEMENTOPS_H_
#define _ARRANGEMENTOPS_H_

#include <string>

class Song;
class Scene;
class Pattern;

// Places a real-clip instance event (clip_index - the clip's own ordinal
// position in track_id's own clip list, Song::getClips(track_id) - what
// actually gets stored is that clip's own stable id instead, Clip.h's own
// comment on why) at `row` on `track_id` in `scene`. Clears away any
// other instance event already placed on that same track within this
// clip's own reach first (Scene::clearInstance() - "removed outright, not
// left as unreachable data"): through row + the clip's own native length
// if it's a one-shot (it has a real, known duration - clearing further
// would destroy later placements it was never going to touch), through
// the scene's own last row if it's looping (no natural bound of its own,
// so no shorter boundary to respect). A no-op if clip_index doesn't
// resolve to a real clip in that track's own list.
void placeClipInstance(const Song & song, Scene & scene, int track_id, int row, int clip_index);

// Places an explicit stop ("instantiate nothing") at `row` on `track_id`
// in `scene`. Clears nothing, unlike placeClipInstance() above - a stop
// adds no new sounding content, so there's nothing of its own to protect
// going forward; whatever's already there from `row` onward was already
// left in a consistent state by whatever was placed before it.
void placeStopInstance(Scene & scene, int track_id, int row);

// Removes `clip_index`'s own clip from track_id's own clip list
// (Song::getClips()) entirely, first clearing away every instance event
// anywhere in the song - every scene, not just one - that referenced it
// (resolved by the clip's own stable id, same as placeClipInstance()'s
// own lookup). A clip's own id is never reused (Song::generateUniqueClipId()),
// so nothing placed afterward could ever collide with a stale leftover
// reference the way reusing a freed vector position could. A no-op if
// clip_index doesn't resolve to a real clip in that track's own list.
//
// Purely in-memory, on principle - nothing on disk changes as a side
// effect of an edit, only ever at an explicit save. A deleted sample
// clip's own sidecar .wav becomes unreferenced, not deleted, here; the
// next Song::save() sweeps orphaned sidecar files itself (see its own
// comment), the same single moment every other in-memory edit here is
// already expected to wait for before touching disk at all.
void deleteClip(Song & song, int track_id, int clip_index);

// The result of resolveInstanceAt() below. `start_row` is the resolved
// instance event's own row - only meaningful when `clip_index` is a real
// clip, but a caller needs it there: rendering that clip's own content
// at `row` means reading it at (row - start_row), wrapped by the clip's
// own length (Pattern::getEffectiveRow()), not at `row` itself.
struct ActiveInstance {
  int clip_index; // a real clip's own ordinal index, Scene::kStopInstance, or Scene::kNoInstance
  int start_row = 0;
};

// Resolves what's active at (track_id, row) in `scene` - the same query
// real (transport) playback's own scheduler needs (SongState.h's own
// renderBlock(), hence this living in src/model/ rather than src/ui/
// alongside PatternBlockOps.h - real playback can't depend on anything
// UI-adjacent). Scans backward for the most recent instance event at or
// before `row`; that clip's own *current* position (resolved from its
// stored stable id, which never assumes it's still wherever it was when
// the instance was placed) if it's still active there (accounting for a
// one-shot's own native length having run out), Scene::kStopInstance if
// the most recent event is an explicit stop, or Scene::kNoInstance if
// nothing was ever placed on this track at or before `row` at all (or a
// stored id no longer resolves to any real clip).
ActiveInstance resolveInstanceAt(const Song & song, const Scene & scene, int track_id, int row);

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
ActiveInstance resolveInstanceForBar(const Song & song, const Scene & scene, int track_id, int bar_start_row, int bar_span);

// What editing (track_id, row) in `scene` should actually read/write -
// the active clip's own leaf Pattern, live-linked to every other
// placement of it (editing through one instance updates them all), when
// resolveInstanceAt() finds a real clip active there; the scene's own
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
EditTarget resolveEditTarget(Song & song, Scene & scene, int track_id, int row, const std::string & focused_clip_id = "");

// Read-only counterpart, for rendering - same resolution, but never
// creates a background Pattern entry as a side effect of merely reading
// (unlike Scene::getPatternsByTrack()[track_id], which would insert an
// empty one on every render pass for every track that's never actually
// been written to). `pattern` is never null - falls back to a shared,
// permanently-empty Pattern when there's truly nothing to read, the same
// "always something to hand back" sentinel convention Scene::getNotes()/
// getCommand() already use for the no-Pattern-at-all case.
struct ReadTarget {
  const Pattern * pattern;
  int effective_row; // wrapped by pattern's own length - what to actually read
  int unwrapped_row; // row - (background: 0, a clip: the instance's own start_row) - for a caller that wants to tell a pattern's own real rows apart from a shorter one's repeat (row >= pattern->getLength()), which effective_row can't answer on its own once it's already wrapped
  bool is_instance; // true when `pattern` is a real clip's own Pattern, false for the background - for a caller that wants to show the difference (e.g. PatternEditor's own instance-tinted rows)
  int clip_index; // only meaningful when is_instance - the clip's own ordinal position in track_id's own clip list (ArrangementGrid's own hex digit addresses the same index), for a caller that wants to show which clip this is, not just that one is active
  bool is_focused_override = false; // true when `focused_clip_id` (see resolveEditTarget()'s own comment) drove this resolution, rather than a real placed instance - lets a caller tell the two apart even though both set is_instance
};
ReadTarget resolveReadTarget(const Song & song, const Scene & scene, int track_id, int row, const std::string & focused_clip_id = "");

#endif
