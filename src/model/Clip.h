#ifndef _CLIP_H_
#define _CLIP_H_

#include "Pattern.h"
#include "SongObject.h"

#include <unordered_map>

// A reusable, shareable unit of musical content, keyed by track_id - one
// Pattern per track it touches. Only ever has one entry today (the
// originating leaf track's own notes), but the storage shape already
// supports more: a nested Effect track's own automation, captured
// alongside the leaf track's own content at creation time, would just be
// another entry in the same map, without needing to change this class's
// own shape.
//
// Distinct from a Scene's own inline Pattern in one crucial way: a clip
// is a single shared object that can be placed at more than one position
// at once, and editing it through any one of those updates every other
// placement immediately. A Scene's own inline content is never shared
// this way - it's always a plain, independent copy.
//
// Extends SongObject for its own display name (getName()/setName(),
// inherited as-is) - a clip's name is the clip's own property, not its
// leaf Pattern's; a scene's own inline Pattern has no name at all.
class Clip : public SongObject {
 public:
  explicit Clip(int leaf_track_id) : leaf_track_id_(leaf_track_id) { }

  int getLeafTrackId() const { return leaf_track_id_; }

  const std::unordered_map<int, Pattern> & getPatternsByTrack() const { return patterns_by_track_; }
  std::unordered_map<int, Pattern> & getPatternsByTrack() { return patterns_by_track_; }

  // The leaf track's own Pattern - present unconditionally the moment a
  // Clip exists at all (every other track_id in getPatternsByTrack() is
  // optional, added only once nested-Effect capture exists).
  Pattern & getLeafPattern() { return patterns_by_track_[leaf_track_id_]; }
  const Pattern & getLeafPattern() const { return patterns_by_track_.at(leaf_track_id_); }

  // A clip's own length, independent of its leaf Pattern's own length_
  // (Pattern.h) - a clip is addressed and triggered outside any scene's
  // row context, so it needs a real length of its own rather than
  // deferring to a context_length the way a scene's own inline Pattern
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
  // a scene's own inline Pattern (ordinary transport-driven playback,
  // bounded by the scene/song's own row range regardless) has no
  // equivalent and isn't a Clip in the first place.
  bool isLooping() const { return loop_; }
  void setLooping(bool loop) { loop_ = loop; }

  // Reads/writes just name_ (via the SongObject base)/loop_/length_
  // (<clip name="..." loop="..." length="...">) - id_ is unused, same as
  // Pattern's own loadParameters()/storeParameters().
  void loadParameters(const ParameterSource & input) override {
    setName(input.get<std::string>("name"));
    setLooping(input.get<bool>("loop", true));
    setLength(input.get<int>("length", 0));
  }

  void storeParameters(ParameterSource & output) const override {
    if (!getName().empty()) output.set("name", getName());
    output.set("loop", isLooping(), true);
    output.set("length", getLength(), 0);
  }

 private:
  int leaf_track_id_;
  std::unordered_map<int, Pattern> patterns_by_track_;
  bool loop_ = true;
  int length_ = 0;
};

#endif
