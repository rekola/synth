#ifndef _CLIP_H_
#define _CLIP_H_

#include "Pattern.h"
#include "SongObject.h"
#include "SampleContent.h"
#include "WaveformPeaks.h"

#include <deque>

// A reusable, shareable unit of musical content for one leaf track - its
// own note/command Pattern. Deliberately not keyed by track_id the way
// Section's own per-row content is: a nested Effect track's automation
// captured alongside a clip would be redundant once a command can target
// any of its parent tracks directly from that one Pattern (`Command`'s
// own device-index digit - planned, not implemented beyond the track's
// own chain position yet, see CLAUDE.md's own command-namespace note), so
// there's no second track's worth of content a clip will ever need to
// hold.
//
// A clip belonging to a SampleTrack carries raw audio instead of Pattern
// content - see getSampleContent()/hasSample() below - via one or more
// SampleContent layers (getSampleLayers()), the sample-content sibling of
// pattern_ above. Overdubbing a SampleTrack clip appends a new layer
// rather than replacing what's already there (Controller::
// beginSampleCapture()'s own comment) - real audio genuinely sums, the
// same way two simultaneously-triggered voices already mix on any other
// track, so each take stays independently addressable (and, in
// principle, removable) rather than being destructively baked into one
// buffer the moment a second take arrives. A single-take clip - the
// overwhelming majority - has exactly one layer, and getSampleContent()
// is the layer-0 convenience every such call site still uses unchanged;
// mixing note-automation content with sample content on one clip isn't
// supported, but which of the two a given Clip actually uses is a matter
// of which one has real content, not which one physically exists.
//
// Distinct from a Section's own inline Pattern in one crucial way: a clip
// is a single shared object that can be placed at more than one position
// at once, and editing it through any one of those updates every other
// placement immediately. A Section's own inline content is never shared
// this way - it's always a plain, independent copy.
//
// Extends SongObject for its own display name (getName()/setName(),
// inherited as-is) - a clip's name is the clip's own property, not its
// leaf Pattern's; a section's own inline Pattern has no name at all. Also
// for id_/getId()/setId() - Song::addClip() assigns one when a clip is
// created without one already set (see its own comment), same as a track
// created through the UI; see this field's own comment further down for
// what a clip's id is actually for.
class Clip : public SongObject {
 public:
  explicit Clip(int leaf_track_id) : leaf_track_id_(leaf_track_id) { }

  int getLeafTrackId() const { return leaf_track_id_; }

  // The leaf track's own Pattern - always present, even on a freshly
  // constructed Clip (a default-constructed Pattern is already a valid,
  // empty one - see isEmpty() below).
  Pattern & getLeafPattern() { return pattern_; }
  const Pattern & getLeafPattern() const { return pattern_; }

  // Whether this clip has any real content at all - either kind, note or
  // audio (hasSample() below; a SampleTrack clip's own Pattern is never
  // touched, so checking pattern_ alone would misreport every populated
  // sample clip as empty). Distinguishes a genuinely unused scene slot
  // (Song::ensureClipAt()'s own filler, or a hand-authored `<clip/>` in
  // the song XML) from a real, if currently silent, take - e.g. Session
  // View's own per-pad display (LaunchpadManager.cpp) and SessionView's
  // own row rendering both need to tell them apart.
  bool isEmpty() const { return pattern_.isEmpty() && !hasSample(); }

  // A clip's own length, independent of its leaf Pattern's own length_
  // (Pattern.h) - a clip is addressed and triggered outside any section's
  // row context, so it needs a real length of its own rather than
  // deferring to a context_length the way a section's own inline Pattern
  // does. 0 means "not given a length of its own" (see Pattern.h's own
  // comment on that same convention); callers already clamp it to at
  // least 1 before using it (LaunchpadManager::triggerClipStep()).
  int getLength() const { return length_; }
  void setLength(int length) { length_ = length; }

  // Session view's own clip-launch loop toggle - true (the default)
  // repeats indefinitely once triggered, matching Pattern::
  // getEffectiveRow()'s own unconditional modulo and every other
  // playback path's behavior. false makes it a one-shot: LaunchpadManager::
  // triggerClipStep() releases the track's voices and stops
  // triggering it, rather than wrapping back to row 0, once it's played
  // through its own length once. Scoped to Session-view triggering only -
  // a section's own inline Pattern (ordinary transport-driven playback,
  // bounded by the section/song's own row range regardless) has no
  // equivalent and isn't a Clip in the first place.
  bool isLooping() const { return loop_; }
  void setLooping(bool loop) { loop_ = loop; }

  // Audio content for a SampleTrack's own clip - see SampleContent.h.
  // hasSample() is true the moment *any* layer has a real buffer - empty
  // for every other track type's clip, and for a fresh/filler one with no
  // layers yet at all. getSampleLayers() is the full list, in recording
  // order (layer 0 the original take, each later one a successive
  // overdub) - kept as independently addressable takes rather than
  // destructively summed the moment a second one arrives; getMixedContent()
  // below is what a real trigger or a background-bed bake actually plays.
  bool hasSample() const {
    for (auto & layer : sample_layers_) if (layer.getBuffer()) return true;
    return false;
  }
  const std::deque<SampleContent> & getSampleLayers() const { return sample_layers_; }
  std::deque<SampleContent> & getSampleLayers() { return sample_layers_; }
  // Layer 0, auto-created on first use - the single-take convenience
  // every call site that doesn't care about overdubbing still uses
  // unchanged, the same "empty member, not an absent one" convention
  // pattern_/isEmpty() above use (a fresh/filler Clip has zero layers, so
  // this is the one accessor that actually needs to create one, unlike
  // getLeafPattern()'s own always-already-there member).
  SampleContent & getSampleContent() {
    if (sample_layers_.empty()) sample_layers_.emplace_back();
    return sample_layers_[0];
  }
  const SampleContent & getSampleContent() const {
    static const SampleContent kEmpty;
    return sample_layers_.empty() ? kEmpty : sample_layers_[0];
  }

  // What a real trigger (SampleTrackState::triggerClip()) actually plays,
  // and what ArrangementOps.cpp's own mergeClipToBackground() bakes into a
  // section's background bed: layer 0 directly when there's at most one
  // real layer (the overwhelming majority of clips - no copying, no
  // resampling, the exact same object getSampleContent() already
  // returns), or the cached, pre-mixed sum of every layer once there's
  // more than one. Never rebuilt in here, or lazily on first read from a
  // stale cache - both would risk a synchronous resample/mix pass
  // (rebuildMixedContent() below, Clip.cpp) running on the audio thread,
  // exactly the kind of work resolveSampleAudio() (SampleTrack.h) is
  // documented as unsafe for there. A stale (not-yet-rebuilt) cache after
  // a fresh overdub layer was just added simply falls back to layer 0
  // alone - the original take, still musically correct to hear, since the
  // new layer isn't finished recording yet anyway.
  const SampleContent & getMixedContent() const {
    return (sample_layers_.size() <= 1 || !mixed_content_valid_) ? getSampleContent() : mixed_content_cache_;
  }
  // Rebuilds getMixedContent()'s own cache from every current layer -
  // Controller::finishSampleCapture() (a new layer just finished) and
  // Song.cpp's own clip loader (a multi-layer clip freshly read from XML)
  // are the only two places a clip's own layer set actually changes, and
  // both already run off the audio thread with the song's own tempo/
  // output rate close at hand - see Clip.cpp for what actually happens
  // inside (resolveSampleAudio() + mixIntoSampleContent(), both
  // SampleTrack.h). A no-op (and leaves the cache invalid) when there's
  // at most one layer - getMixedContent() never even looks at the cache
  // in that case, so there's nothing to build.
  void rebuildMixedContent(int output_rate, int song_tempo);

  // Starts a genuinely new layer (an overdub take) and returns it,
  // leaving every existing layer completely untouched - Controller::
  // beginSampleCapture()'s own fresh-vs-overdub decision is the one
  // caller. Immediately invalidates getMixedContent()'s own cache (the
  // new layer isn't finished recording yet, so there's nothing new worth
  // rebuilding for right now regardless - rebuildMixedContent() is the
  // caller's own job again once it actually is), rather than leaving a
  // now-incomplete mix silently served as if it were still current.
  SampleContent & addSampleLayer() {
    sample_layers_.emplace_back();
    mixed_content_valid_ = false;
    return sample_layers_.back();
  }

  // The row-indexed RMS amplitude cache PatternEditor's own waveform-box
  // rendering reads from (WaveformPeaks.h's own comment has the full
  // reasoning on why row-, not time-, indexed). A thin forward to layer
  // 0's own copy - it owns the buffer/trim points this is built from, so
  // it's the one that can actually invalidate it directly, from its own
  // setBuffer()/setInPoint()/setOutPoint(), rather than a caller out here
  // having to guess staleness by remembering and comparing old values.
  // Layer 0 only, not a composite of every layer once a clip has been
  // overdubbed - showing the true mixed waveform would need a second
  // cache built by summing across layers; not done yet, so the waveform
  // box under-represents an overdubbed take's own later layers for now.
  // `getLength()` (this class's own field, SampleContent has no notion of
  // it) and `subrows_per_row` (a runtime choice, UIPlane::
  // canRenderSextants()) are the two things only this call site actually
  // knows, so they're passed in as plain parameters rather than something
  // SampleContent tracks itself. getLength() > 0 ? getLength() : 1, not
  // getLength() directly - the same "0 means not given one yet, treat it
  // as 1" convention resolveInstanceAt()/resolveEditTarget()/
  // placeClipInstance() already apply, so a hand-authored or file-
  // referenced clip with no explicit length="" attribute still gets a
  // real, buildable single-row cache instead of silently resolving to
  // nothing.
  const WaveformPeaks & getWaveformPeaks(int subrows_per_row) const {
    static const WaveformPeaks kEmpty;
    return hasSample() ? getSampleContent().getWaveformPeaks(getLength() > 0 ? getLength() : 1, subrows_per_row) : kEmpty;
  }

  // getId()/setId() (inherited from SongObject, same field a track's own
  // id uses - unlike Pattern, which leaves it unused) are this class's
  // own stable identity - alphanumeric, assigned once by Song::addClip()
  // (see its own comment), distinct from
  // this clip's position in Song::getClips(track_id). Every consumer-
  // facing API (placeClipInstance()/resolveInstanceAt(), ArrangementGrid's
  // own digit, a Launchpad pad row) still addresses a clip by that
  // position, since it's what's physically meaningful there (a pad row, a
  // single hex digit) - the id is only ever used internally, as what
  // actually gets stored in a placed instance event (ArrangementOps.cpp),
  // so a clip already referenced from somewhere keeps resolving to itself
  // even if something else ahead of it in the same track's list is later
  // deleted/reordered (Phase E), rather than silently reinterpreting a
  // now-stale position as whichever different clip happens to occupy it
  // afterward.

  // Reads/writes id_/name_ (via the SongObject base)/loop_/length_
  // (<clip id="..." name="..." loop="..." length="...">). Neither a
  // sample clip's own nested <sample> child (SampleContent's own
  // in/out/originalTempo, plus its `file` reference) nor a note clip's
  // own <pattern> child are read/written here - both need more than a
  // flat attribute source (disk I/O and the song's own directory, for
  // <sample>; a separate element entirely, for <pattern>) - Song.cpp's
  // clip reader/writer handles both directly.
  void loadParameters(const ParameterSource & input) override {
    SongObject::loadParameters(input);
    setLooping(input.get<bool>("loop", true));
    setLength(input.get<int>("length", 0));
  }

  void storeParameters(ParameterSource & output) const override {
    SongObject::storeParameters(output);
    output.set("loop", isLooping(), true);
    output.set("length", getLength(), 0);
  }

 private:
  int leaf_track_id_;
  Pattern pattern_;
  // A deque, not a vector - SongState.h's own render-time pending-sample-
  // start path keeps a raw pointer to layer 0 alive across a single
  // render block (RenderContext.h's own SampleTrackEvent comment), and a
  // vector's push_back can reallocate its whole backing store, silently
  // invalidating that pointer the moment a fresh overdub layer is added;
  // a deque never invalidates references to existing elements on
  // push_back/emplace_back, only iterators, so an existing layer's own
  // address is stable for its whole lifetime.
  std::deque<SampleContent> sample_layers_;
  // getMixedContent()/rebuildMixedContent()'s own cache - see their own
  // comments. Never read at all (mixed_content_valid_ or not) unless
  // sample_layers_.size() > 1.
  SampleContent mixed_content_cache_;
  bool mixed_content_valid_ = false;
  bool loop_ = true;
  int length_ = 0;
};

#endif
