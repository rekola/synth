#include "ClipView.h"

using namespace scoreschema;

std::shared_ptr<AudioBuffer>
SampleLayerView::getBuffer() const {
  return context_.samples->asset(doc::get(*context_.document, node_, kSampleAsset));
}

void
SampleLayerView::setBuffer(std::shared_ptr<AudioBuffer> buffer) {
  doc::set(*context_.document, node_, kSampleAsset, context_.samples->addAsset(std::move(buffer)));
}

doc::NodeId
SampleLayerView::create(ScoreContext context, const SampleContent & content) {
  std::vector<std::pair<std::string, doc::Value> > props;
  auto add = [&](auto prop, auto value) {
    if (!doc::isDefault(value, prop.def)) props.emplace_back(prop.key, doc::toValue(value));
  };
  add(kSampleAsset, context.samples->addAsset(content.getBuffer()));
  add(kSampleIn, content.getInPoint());
  add(kSampleOut, content.getOutPoint());
  add(kSampleOriginalTempo, static_cast<int>(content.getOriginalTempo()));
  add(kSampleNativeRate, content.getNativeSampleRate());
  return context.document->create("sample", std::move(props));
}

doc::NodeId
ClipView::create(ScoreContext context, int leaf_track_id) {
  std::vector<std::pair<std::string, doc::Value> > props;
  if (!doc::isDefault(leaf_track_id, kClipTrack.def)) props.emplace_back(kClipTrack.key, doc::toValue(leaf_track_id));
  auto clip = context.document->create("clip", std::move(props));
  context.document->appendToDetached(clip, kClipPatternSlot, PatternView::create(*context.document));
  return clip;
}

PatternView
ClipView::getLeafPattern() const {
  auto n = context_.document->get(node_);
  auto list = n ? n->children(kClipPatternSlot) : nullptr;
  return list && !list->empty() ? PatternView(context_.document, list->front()) : PatternView();
}

size_t
ClipView::sampleLayerCount() const {
  auto n = context_.document->get(node_);
  auto list = n ? n->children(kClipSamplesSlot) : nullptr;
  return list ? list->size() : 0;
}

SampleLayerView
ClipView::sampleLayer(size_t index) const {
  auto list = context_.document->get(node_)->children(kClipSamplesSlot);
  return list && index < list->size() ? SampleLayerView(context_, (*list)[index]) : SampleLayerView();
}

std::vector<SampleLayerView>
ClipView::getSampleLayers() const {
  std::vector<SampleLayerView> layers;
  for (size_t i = 0; i < sampleLayerCount(); i++) layers.push_back(sampleLayer(i));
  return layers;
}

bool
ClipView::hasSample() const {
  for (size_t i = 0; i < sampleLayerCount(); i++) {
    if (sampleLayer(i).content().getBuffer()) return true;
  }
  return false;
}

SampleLayerView
ClipView::firstSampleLayer() {
  if (sampleLayerCount() == 0) return addSampleLayer();
  return sampleLayer(0);
}

SampleLayerView
ClipView::addSampleLayer() {
  auto layer = SampleLayerView::create(context_, SampleContent());
  context_.document->insertChild(node_, kClipSamplesSlot, sampleLayerCount(), layer);
  return SampleLayerView(context_, layer);
}

const SampleContent &
ClipView::getSampleContent() const {
  static const SampleContent kEmpty;
  return sampleLayerCount() == 0 ? kEmpty : sampleLayer(0).content();
}

const WaveformPeaks &
ClipView::getWaveformPeaks(int subrows_per_row) const {
  static const WaveformPeaks kEmpty;
  return hasSample() ? getMixedContent().getWaveformPeaks(getLength() > 0 ? getLength() : 1, subrows_per_row) : kEmpty;
}

void
ClipView::assign(const Clip & clip) {
  doc::Transaction t(*context_.document, "assign clip");
  setId(clip.getId());
  setName(clip.getName());
  setLeafTrackId(clip.getLeafTrackId());
  setLength(clip.getLength());
  setLooping(clip.isLooping());
  setStopButton(clip.hasStopButton());
  getLeafPattern().assign(clip.getLeafPattern());
  for (auto i = sampleLayerCount(); i > 0; i--) context_.document->removeChild(node_, kClipSamplesSlot, i - 1);
  for (auto & layer : clip.getSampleLayers()) {
    context_.document->insertChild(node_, kClipSamplesSlot, sampleLayerCount(), SampleLayerView::create(context_, layer));
  }
}

Clip
ClipView::toClip() const {
  Clip clip(getLeafTrackId());
  clip.setId(getId());
  clip.setName(getName());
  clip.setLength(getLength());
  clip.setLooping(isLooping());
  clip.setStopButton(hasStopButton());
  clip.getLeafPattern() = getLeafPattern().toPattern();
  for (size_t i = 0; i < sampleLayerCount(); i++) clip.addSampleLayer() = sampleLayer(i).content();
  if (sampleLayerCount() > 1) clip.setMixedContent(getMixedContent());
  return clip;
}

size_t
ClipList::size() const {
  auto n = context_.document->get(context_.document->root());
  auto list = n->children(slotName(track_id_));
  return list ? list->size() : 0;
}

ClipView
ClipList::operator[](size_t index) const {
  auto list = context_.document->get(context_.document->root())->children(slotName(track_id_));
  return list && index < list->size() ? ClipView(context_, (*list)[index]) : ClipView();
}
