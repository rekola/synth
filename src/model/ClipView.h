#ifndef _CLIPVIEW_H_
#define _CLIPVIEW_H_

#include "Clip.h"
#include "PatternView.h"
#include "SampleContent.h"
#include "SampleStore.h"
#include "ScoreContext.h"
#include "ScoreSchema.h"

#include <memory>
#include <string>

// One layer of a sample clip's audio (or a track's background bed), as a
// handle on its "sample" node. The audio is shared with the store, not copied.
class SampleLayerView {
 public:
  SampleLayerView() = default;
  SampleLayerView(ScoreContext context, doc::NodeId node) : context_(context), node_(node) { }

  bool valid() const { return context_.document && context_.document->get(node_); }
  doc::NodeId node() const { return node_; }

  std::shared_ptr<AudioBuffer> getBuffer() const;
  void setBuffer(std::shared_ptr<AudioBuffer> buffer);
  float getInPoint() const { return doc::get(*context_.document, node_, scoreschema::kSampleIn); }
  void setInPoint(float seconds) { doc::set(*context_.document, node_, scoreschema::kSampleIn, seconds); }
  float getOutPoint() const { return doc::get(*context_.document, node_, scoreschema::kSampleOut); }
  void setOutPoint(float seconds) { doc::set(*context_.document, node_, scoreschema::kSampleOut, seconds); }
  short getOriginalTempo() const { return static_cast<short>(doc::get(*context_.document, node_, scoreschema::kSampleOriginalTempo)); }
  void setOriginalTempo(short bpm) { doc::set(*context_.document, node_, scoreschema::kSampleOriginalTempo, static_cast<int>(bpm)); }
  int getNativeSampleRate() const { return doc::get(*context_.document, node_, scoreschema::kSampleNativeRate); }
  void setNativeSampleRate(int rate) { doc::set(*context_.document, node_, scoreschema::kSampleNativeRate, rate); }

  // The playable content (SampleStore::content()): buffer, trim points and
  // tempo together, with the caches that belong to it.
  const SampleContent & content() const { return context_.samples->content(*context_.document, node_); }

  // A detached "sample" node holding `content`'s audio and settings.
  static doc::NodeId create(ScoreContext context, const SampleContent & content);

 private:
  ScoreContext context_;
  doc::NodeId node_ = doc::kNoNode;
};

// A clip in the document: the live counterpart of the value class Clip,
// which it replaces for anything that edits or displays the song. A handle,
// not content - see PatternView.
class ClipView {
 public:
  ClipView() = default;
  ClipView(ScoreContext context, doc::NodeId node) : context_(context), node_(node) { }

  bool valid() const { return context_.document && context_.document->get(node_); }
  doc::NodeId node() const { return node_; }

  const std::string & getId() const { return doc::getRef(*context_.document, node_, scoreschema::kClipId); }
  void setId(std::string id) { doc::set(*context_.document, node_, scoreschema::kClipId, std::move(id)); }
  const std::string & getName() const { return doc::getRef(*context_.document, node_, scoreschema::kClipName); }
  void setName(std::string name) { doc::set(*context_.document, node_, scoreschema::kClipName, std::move(name)); }
  int getLeafTrackId() const { return doc::get(*context_.document, node_, scoreschema::kClipTrack); }
  void setLeafTrackId(int id) { doc::set(*context_.document, node_, scoreschema::kClipTrack, id); }
  int getLength() const { return doc::get(*context_.document, node_, scoreschema::kClipLength); }
  void setLength(int length) { doc::set(*context_.document, node_, scoreschema::kClipLength, length); }
  bool isLooping() const { return doc::get(*context_.document, node_, scoreschema::kClipLoop); }
  void setLooping(bool loop) { doc::set(*context_.document, node_, scoreschema::kClipLoop, loop); }
  bool hasStopButton() const { return doc::get(*context_.document, node_, scoreschema::kClipStopButton); }
  void setStopButton(bool stop_button) { doc::set(*context_.document, node_, scoreschema::kClipStopButton, stop_button); }

  PatternView getLeafPattern() const;
  bool isEmpty() const { return getLeafPattern().isEmpty() && !hasSample(); }

  bool hasSample() const;
  size_t sampleLayerCount() const;
  SampleLayerView sampleLayer(size_t index) const;
  // Layer 0, created if the clip has none yet.
  SampleLayerView firstSampleLayer();
  // A new, empty layer after the others (an overdub take).
  SampleLayerView addSampleLayer();
  const SampleContent & getSampleContent() const;
  // What a trigger plays - see SampleStore::mixed().
  const SampleContent & getMixedContent() const { return context_.samples->mixed(*context_.document, node_); }
  void rebuildMixedContent(int output_rate, int song_tempo) { context_.samples->rebuildMixed(*context_.document, node_, output_rate, song_tempo); }
  const WaveformPeaks & getWaveformPeaks(int subrows_per_row) const;

  // Replaces this clip's content and settings with `clip`'s.
  void assign(const Clip & clip);
  // The plain value, for the published copy and the clipboard.
  Clip toClip() const;

  // A detached clip node with a pattern and nothing else yet.
  static doc::NodeId create(ScoreContext context, int leaf_track_id);

 private:
  ScoreContext context_;
  doc::NodeId node_ = doc::kNoNode;
};

// A track's clips, in scene order: a handle on the song root's
// "clips:<track id>" slot.
class ClipList {
 public:
  ClipList(ScoreContext context, int track_id) : context_(context), track_id_(track_id) { }

  static std::string slotName(int track_id) { return "clips:" + std::to_string(track_id); }

  size_t size() const;
  bool empty() const { return size() == 0; }
  ClipView operator[](size_t index) const;
  ClipView front() const { return (*this)[0]; }
  ClipView back() const { return (*this)[size() - 1]; }

  class Iterator {
   public:
    Iterator(const ClipList * list, size_t index) : list_(list), index_(index) { }
    ClipView operator*() const { return (*list_)[index_]; }
    Iterator & operator++() { index_++; return *this; }
    bool operator!=(const Iterator & other) const { return index_ != other.index_; }
    bool operator==(const Iterator & other) const { return index_ == other.index_; }

   private:
    const ClipList * list_;
    size_t index_;
  };
  Iterator begin() const { return Iterator(this, 0); }
  Iterator end() const { return Iterator(this, size()); }

 private:
  ScoreContext context_;
  int track_id_;
};

#endif
