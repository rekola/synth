#ifndef _SAMPLESTORE_H_
#define _SAMPLESTORE_H_

#include "SampleContent.h"
#include "../audio/AudioBuffer.h"
#include "../doc/Document.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// The audio behind a song's "sample" nodes. A node only says which buffer
// (an asset id) and how to play it; the buffers live here, shared and never
// copied, so the journal can hold on to a deleted take for as long as undo
// can bring it back. Also caches, per layer, the SampleContent a view hands
// out - one object per layer for as long as its properties stay the same, so
// the tempo-stretch work done on the audio thread and the waveform the UI
// has drawn survive a republish.
class SampleStore {
 public:
  // A store for the views that point at nothing; it holds whatever they are
  // given and nobody reads it.
  static SampleStore & inert();

  int64_t addAsset(std::shared_ptr<AudioBuffer> buffer);
  std::shared_ptr<AudioBuffer> asset(int64_t id) const;

  // The layer's content. Valid until the next call that can drop it
  // (collect()); rebuilt when the node's properties have changed.
  const SampleContent & content(const doc::Document & document, doc::NodeId layer);

  // What a trigger of `clip` plays: its one layer, or the mix of all of them
  // once rebuildMixed() has run for the current layers - until then (a take
  // still recording) the first layer alone.
  const SampleContent & mixed(const doc::Document & document, doc::NodeId clip);
  void rebuildMixed(const doc::Document & document, doc::NodeId clip, int output_rate, int song_tempo);

  // Drops assets and cached content no node (attached or in history) refers
  // to any more.
  void collect(const doc::Document & document);

 private:
  struct Signature {
    int64_t asset = 0;
    float in = 0, out = 0;
    int original_tempo = 0, native_rate = 0;
    bool operator==(const Signature & o) const { return asset == o.asset && in == o.in && out == o.out && original_tempo == o.original_tempo && native_rate == o.native_rate; }
  };
  struct Cached {
    Signature signature;
    SampleContent content;
  };
  struct Mix {
    std::vector<Signature> layers;
    SampleContent content;
  };

  static Signature signatureOf(const doc::Document & document, doc::NodeId layer);
  std::vector<doc::NodeId> layersOf(const doc::Document & document, doc::NodeId clip) const;

  int64_t next_asset_ = 1;
  std::unordered_map<int64_t, std::shared_ptr<AudioBuffer> > assets_;
  std::unordered_map<const AudioBuffer *, int64_t> asset_ids_; // one id per buffer
  std::unordered_map<doc::NodeId, Cached> contents_;
  std::unordered_map<doc::NodeId, Mix> mixes_;
};

#endif
