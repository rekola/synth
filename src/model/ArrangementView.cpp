#include "ArrangementView.h"

using namespace scoreschema;

doc::NodeId
ArrangementView::create(doc::Document & document) {
  return document.create("arrangement");
}

const std::vector<doc::NodeId> &
ArrangementView::ids(const char * slot) const {
  static const std::vector<doc::NodeId> kNone;
  auto n = context_.document->get(node_);
  auto list = n ? n->children(slot) : nullptr;
  return list ? *list : kNone;
}

size_t
ArrangementView::patternIndex(int track_id) const {
  auto & list = ids(kArrangementPatternsSlot);
  size_t lo = 0, hi = list.size();
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    if (doc::get(*context_.document, list[mid], kPatternTrack) < track_id) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

size_t
ArrangementView::backgroundIndex(int track_id) const {
  auto & list = ids(kArrangementBackgroundsSlot);
  size_t lo = 0, hi = list.size();
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    if (doc::get(*context_.document, list[mid], kSampleTrack) < track_id) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

size_t
ArrangementView::instanceIndex(int track_id, int row) const {
  auto & list = ids(kArrangementInstancesSlot);
  size_t lo = 0, hi = list.size();
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    auto t = doc::get(*context_.document, list[mid], kInstanceTrack);
    if (t < track_id || (t == track_id && doc::get(*context_.document, list[mid], kInstanceRow) < row)) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

PatternView
ArrangementView::findPattern(int track_id) const {
  auto index = patternIndex(track_id);
  auto & list = ids(kArrangementPatternsSlot);
  if (index < list.size() && doc::get(*context_.document, list[index], kPatternTrack) == track_id) return PatternView(context_.document, list[index]);
  return PatternView();
}

PatternView
ArrangementView::patternFor(int track_id) {
  auto found = findPattern(track_id);
  if (found.valid()) return found;
  std::vector<std::pair<std::string, doc::Value> > props;
  if (!doc::isDefault(track_id, kPatternTrack.def)) props.emplace_back(kPatternTrack.key, doc::toValue(track_id));
  auto pattern = context_.document->create("pattern", std::move(props));
  context_.document->insertChild(node_, kArrangementPatternsSlot, patternIndex(track_id), pattern);
  return PatternView(context_.document, pattern);
}

std::map<int, PatternView>
ArrangementView::getPatternsByTrack() const {
  std::map<int, PatternView> out;
  for (auto node : ids(kArrangementPatternsSlot)) out.emplace(doc::get(*context_.document, node, kPatternTrack), PatternView(context_.document, node));
  return out;
}

int
ArrangementView::getEffectiveRow(int track_id, int row, int context_length) const {
  auto pattern = findPattern(track_id);
  return pattern.valid() ? pattern.getEffectiveRow(row, context_length) : row;
}

void
ArrangementView::clearNotes(int row, int track_id) {
  auto pattern = findPattern(track_id);
  if (pattern.valid()) pattern.clearNotes(row);
}

void
ArrangementView::deleteNote(int row, int track_id, int column) {
  auto pattern = findPattern(track_id);
  if (pattern.valid()) pattern.deleteNote(row, column);
}

Note
ArrangementView::getNote(int row, int track_id, int note_column) const {
  auto pattern = findPattern(track_id);
  return pattern.valid() ? pattern.getNote(row, note_column) : Note();
}

std::vector<Note>
ArrangementView::getNotes(int row, int track_id) const {
  auto pattern = findPattern(track_id);
  return pattern.valid() ? pattern.getNotes(row) : std::vector<Note>();
}

Command
ArrangementView::getCommand(int row, int track_id) const {
  return getCommand(row, track_id, 0);
}

Command
ArrangementView::getCommand(int row, int track_id, int command_column) const {
  auto pattern = findPattern(track_id);
  return pattern.valid() ? pattern.getCommand(row, command_column) : Command();
}

std::vector<Command>
ArrangementView::getCommandsAt(int row, int track_id) const {
  auto pattern = findPattern(track_id);
  return pattern.valid() ? pattern.getCommandsAt(row) : std::vector<Command>();
}

void
ArrangementView::getTrackInformation(std::unordered_map<int, VisibleTrackInfo> & track_info) const {
  for (auto & [ track_id, pattern ] : getPatternsByTrack()) pattern.updateSubtrackInfo(track_info[track_id]);
}

void
ArrangementView::setInstance(int track_id, int row, const std::string & clip_id) {
  auto index = instanceIndex(track_id, row);
  auto & list = ids(kArrangementInstancesSlot);
  if (index < list.size() && doc::get(*context_.document, list[index], kInstanceTrack) == track_id && doc::get(*context_.document, list[index], kInstanceRow) == row) {
    doc::set(*context_.document, list[index], kInstanceClip, clip_id);
    return;
  }
  std::vector<std::pair<std::string, doc::Value> > props;
  if (!doc::isDefault(track_id, kInstanceTrack.def)) props.emplace_back(kInstanceTrack.key, doc::toValue(track_id));
  if (!doc::isDefault(row, kInstanceRow.def)) props.emplace_back(kInstanceRow.key, doc::toValue(row));
  if (!clip_id.empty()) props.emplace_back(kInstanceClip.key, doc::toValue(clip_id));
  context_.document->insertChild(node_, kArrangementInstancesSlot, index, context_.document->create("instance", std::move(props)));
}

void
ArrangementView::clearInstance(int track_id, int row) {
  auto index = instanceIndex(track_id, row);
  auto & list = ids(kArrangementInstancesSlot);
  if (index < list.size() && doc::get(*context_.document, list[index], kInstanceTrack) == track_id && doc::get(*context_.document, list[index], kInstanceRow) == row) {
    context_.document->removeChild(node_, kArrangementInstancesSlot, index);
  }
}

const std::string &
ArrangementView::getInstance(int track_id, int row) const {
  static const std::string kNone;
  auto index = instanceIndex(track_id, row);
  auto & list = ids(kArrangementInstancesSlot);
  if (index < list.size() && doc::get(*context_.document, list[index], kInstanceTrack) == track_id && doc::get(*context_.document, list[index], kInstanceRow) == row) {
    return doc::getRef(*context_.document, list[index], kInstanceClip);
  }
  return kNone;
}

std::map<unsigned short, std::string>
ArrangementView::getInstancesForTrack(int track_id) const {
  std::map<unsigned short, std::string> out;
  auto & list = ids(kArrangementInstancesSlot);
  for (auto i = instanceIndex(track_id, 0); i < list.size() && doc::get(*context_.document, list[i], kInstanceTrack) == track_id; i++) {
    out[static_cast<unsigned short>(doc::get(*context_.document, list[i], kInstanceRow))] = doc::get(*context_.document, list[i], kInstanceClip);
  }
  return out;
}

std::map<int, std::map<unsigned short, std::string> >
ArrangementView::getInstancesByTrack() const {
  std::map<int, std::map<unsigned short, std::string> > out;
  for (auto node : ids(kArrangementInstancesSlot)) {
    out[doc::get(*context_.document, node, kInstanceTrack)][static_cast<unsigned short>(doc::get(*context_.document, node, kInstanceRow))] = doc::get(*context_.document, node, kInstanceClip);
  }
  return out;
}

SampleLayerView
ArrangementView::sampleBackground(int track_id) const {
  auto index = backgroundIndex(track_id);
  auto & list = ids(kArrangementBackgroundsSlot);
  if (index < list.size() && doc::get(*context_.document, list[index], kSampleTrack) == track_id) return SampleLayerView(context_, list[index]);
  return SampleLayerView();
}

const SampleContent *
ArrangementView::getSampleBackgroundContent(int track_id) const {
  auto layer = sampleBackground(track_id);
  if (!layer.valid()) return nullptr;
  auto & content = layer.content();
  return content.getBuffer() ? &content : nullptr;
}

void
ArrangementView::setSampleBackground(int track_id, const SampleContent & content) {
  doc::Transaction t(*context_.document, "set background");
  auto index = backgroundIndex(track_id);
  auto existing = sampleBackground(track_id);
  if (existing.valid()) context_.document->removeChild(node_, kArrangementBackgroundsSlot, index);
  auto layer = SampleLayerView::create(context_, content);
  if (!doc::isDefault(track_id, kSampleTrack.def)) context_.document->setProperty(layer, kSampleTrack.key, doc::toValue(track_id));
  context_.document->insertChild(node_, kArrangementBackgroundsSlot, index, layer);
}

std::map<int, SampleContent>
ArrangementView::getSampleBackgroundsByTrack() const {
  std::map<int, SampleContent> out;
  for (auto node : ids(kArrangementBackgroundsSlot)) {
    out[doc::get(*context_.document, node, kSampleTrack)] = SampleLayerView(context_, node).content();
  }
  return out;
}

Arrangement
ArrangementView::toArrangement() const {
  Arrangement out;
  for (auto & [ track_id, pattern ] : getPatternsByTrack()) out.setPatternForTrack(track_id, pattern.toPattern());
  for (auto & [ track_id, instances ] : getInstancesByTrack()) {
    for (auto & [ row, clip_id ] : instances) out.setInstance(track_id, row, clip_id);
  }
  for (auto & [ track_id, content ] : getSampleBackgroundsByTrack()) out.getOrCreateSampleBackgroundContent(track_id) = content;
  return out;
}
