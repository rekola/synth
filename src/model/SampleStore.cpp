#include "SampleStore.h"

#include "Clip.h"
#include "ScoreSchema.h"

#include <set>

using namespace scoreschema;

int64_t
SampleStore::addAsset(std::shared_ptr<AudioBuffer> buffer) {
  if (!buffer) return 0;
  auto known = asset_ids_.find(buffer.get());
  if (known != asset_ids_.end()) return known->second;
  auto id = next_asset_++;
  asset_ids_[buffer.get()] = id;
  assets_[id] = std::move(buffer);
  return id;
}

std::shared_ptr<AudioBuffer>
SampleStore::asset(int64_t id) const {
  auto it = assets_.find(id);
  return it == assets_.end() ? nullptr : it->second;
}

SampleStore::Signature
SampleStore::signatureOf(const doc::Document & document, doc::NodeId layer) {
  Signature s;
  s.asset = doc::get(document, layer, kSampleAsset);
  s.in = doc::get(document, layer, kSampleIn);
  s.out = doc::get(document, layer, kSampleOut);
  s.original_tempo = doc::get(document, layer, kSampleOriginalTempo);
  s.native_rate = doc::get(document, layer, kSampleNativeRate);
  return s;
}

const SampleContent &
SampleStore::content(const doc::Document & document, doc::NodeId layer) {
  auto signature = signatureOf(document, layer);
  auto it = contents_.find(layer);
  if (it != contents_.end() && it->second.signature == signature) return it->second.content;
  Cached cached;
  cached.signature = signature;
  cached.content.setBuffer(asset(signature.asset));
  cached.content.setInPoint(signature.in);
  cached.content.setOutPoint(signature.out);
  cached.content.setOriginalTempo(static_cast<short>(signature.original_tempo));
  cached.content.setNativeSampleRate(signature.native_rate);
  return (contents_[layer] = std::move(cached)).content;
}

std::vector<doc::NodeId>
SampleStore::layersOf(const doc::Document & document, doc::NodeId clip) const {
  auto n = document.get(clip);
  auto list = n ? n->children(kClipSamplesSlot) : nullptr;
  return list ? *list : std::vector<doc::NodeId>();
}

const SampleContent &
SampleStore::mixed(const doc::Document & document, doc::NodeId clip) {
  static const SampleContent kEmpty;
  auto layers = layersOf(document, clip);
  if (layers.empty()) return kEmpty;
  if (layers.size() > 1) {
    auto it = mixes_.find(clip);
    if (it != mixes_.end() && it->second.layers.size() == layers.size()) {
      bool current = true;
      for (size_t i = 0; i < layers.size() && current; i++) current = it->second.layers[i] == signatureOf(document, layers[i]);
      if (current) return it->second.content;
    }
  }
  return content(document, layers[0]);
}

void
SampleStore::rebuildMixed(const doc::Document & document, doc::NodeId clip, int output_rate, int song_tempo) {
  auto layers = layersOf(document, clip);
  if (layers.size() <= 1) {
    mixes_.erase(clip);
    return;
  }
  // Clip already knows how to mix its layers; build one to do it with.
  Clip scratch(0);
  for (auto layer : layers) scratch.addSampleLayer() = content(document, layer);
  scratch.rebuildMixedContent(output_rate, song_tempo);
  Mix mix;
  for (auto layer : layers) mix.layers.push_back(signatureOf(document, layer));
  mix.content = scratch.getMixedContent();
  mixes_[clip] = std::move(mix);
}

void
SampleStore::collect(const doc::Document & document) {
  std::set<int64_t> used_assets;
  std::set<doc::NodeId> used_layers, used_clips;
  // Anything still in the table of nodes - attached, or kept for undo.
  document.forEachNode([&](const doc::Node & node) {
    if (node.type == "sample") {
      used_layers.insert(node.id);
      if (auto v = node.find(kSampleAsset.key)) used_assets.insert(doc::fromValue<int64_t>(v, 0));
    } else if (node.type == "clip") {
      used_clips.insert(node.id);
    }
  });
  for (auto it = assets_.begin(); it != assets_.end();) {
    if (used_assets.count(it->first)) {
      ++it;
    } else {
      asset_ids_.erase(it->second.get());
      it = assets_.erase(it);
    }
  }
  for (auto it = contents_.begin(); it != contents_.end();) it = used_layers.count(it->first) ? std::next(it) : contents_.erase(it);
  for (auto it = mixes_.begin(); it != mixes_.end();) it = used_clips.count(it->first) ? std::next(it) : mixes_.erase(it);
}
