#ifndef _CLIP_H_
#define _CLIP_H_

#include "Pattern.h"
#include "SongObject.h"
#include "SampleContent.h"
#include "WaveformPeaks.h"

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
// content - see getSampleContent()/hasSample() below - via a
// SampleContent member, the sample-content sibling of pattern_ above and
// held the same way (a plain value, always present - never null, unlike
// SampleContent's own buffer_ inside it, which really is optional and is
// what hasSample() actually tests); mixing note-automation content with
// sample content on one clip isn't supported, but which of the two a
// given Clip actually uses is a matter of which one has real content, not
// which one physically exists.
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
  // getBuffer() == nullptr (the default) for every other track type's
  // clip, the same "empty member, not an absent one" convention pattern_/
  // isEmpty() above use - so, like getLeafPattern(), always present and
  // returned by reference; nothing to lazily create on first write
  // anymore.
  bool hasSample() const { return sample_content_.getBuffer() != nullptr; }
  const SampleContent & getSampleContent() const { return sample_content_; }
  SampleContent & getSampleContent() { return sample_content_; }

  // The row-indexed RMS amplitude cache PatternEditor's own waveform-box
  // rendering reads from (WaveformPeaks.h's own comment has the full
  // reasoning on why row-, not time-, indexed). A thin forward to
  // SampleContent's own copy - it owns the buffer/trim points this is
  // built from, so it's the one that can actually invalidate it directly,
  // from its own setBuffer()/setInPoint()/setOutPoint(), rather than a
  // caller out here having to guess staleness by remembering and
  // comparing old values. `getLength()` (this class's own field,
  // SampleContent has no notion of it) and `subrows_per_row` (a runtime
  // choice, UIPlane::canRenderSextants()) are the two things only this
  // call site actually knows, so they're passed in as plain parameters
  // rather than something SampleContent tracks itself. getLength() > 0 ?
  // getLength() : 1, not getLength() directly - the same "0 means not
  // given one yet, treat it as 1" convention resolveInstanceAt()/
  // resolveEditTarget()/placeClipInstance() already apply, so a hand-
  // authored or file-referenced clip with no explicit length="" attribute
  // still gets a real, buildable single-row cache instead of silently
  // resolving to nothing.
  const WaveformPeaks & getWaveformPeaks(int subrows_per_row) const {
    static const WaveformPeaks kEmpty;
    return hasSample() ? sample_content_.getWaveformPeaks(getLength() > 0 ? getLength() : 1, subrows_per_row) : kEmpty;
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
  SampleContent sample_content_;
  bool loop_ = true;
  int length_ = 0;
};

#endif
