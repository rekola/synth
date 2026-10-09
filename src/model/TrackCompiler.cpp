#include "TrackCompiler.h"

#include "TrackNodes.h"
#include "../bus/BusEffectRegistry.h"
#include "../instruments/GenericInstrument.h"
#include "../instruments/InstrumentProvider.h"

#include <algorithm>
#include <cstdlib>

using tracknodes::NodeParameterSource;

// The bus effects here only hold parameters; the sample rate never matters.
static constexpr int kPlaceholderBusSampleRate = 44100;

doc::NodeId
TrackCompiler::adopt(doc::Document & document, std::shared_ptr<Track> track) {
  auto id = tracknodes::trackToNode(document, *track);
  // trackToNode() appended sub-nodes in the order of track->getChildren()
  // (generators first), so walk both together.
  struct Walk {
    doc::Document & document;
    std::vector<std::pair<doc::NodeId, std::shared_ptr<Track> > > & out;
    void run(doc::NodeId node, const std::shared_ptr<Track> & object) {
      out.emplace_back(node, object);
      auto n = document.get(node);
      auto slot = n->children(tracknodes::kChildrenSlot);
      if (!slot) return;
      size_t next = 0;
      for (auto child : *slot) {
        auto c = document.get(child);
        if (c->type == tracknodes::kGeneratorType) continue;
        run(child, object->getChildren()[next++]);
      }
    }
  };
  Walk{ document, adopted_ }.run(id, track);
  return id;
}

void
TrackCompiler::commitAdopted(const doc::Document & document) {
  for (auto & [ node, object ] : adopted_) entries_[node] = Entry{ document.subtreeRevision(node), object };
  adopted_.clear();
}

uint64_t
TrackCompiler::stamp(const doc::Document & document, const Roots & roots) {
  auto newest = std::max(document.subtreeRevision(roots.master), document.subtreeRevision(roots.pool));
  for (auto bus : roots.bus) newest = std::max(newest, document.subtreeRevision(bus));
  return newest;
}

std::shared_ptr<Track>
TrackCompiler::compileNode(const doc::Document & document, doc::NodeId id, const InstrumentProvider * provider, uint64_t * revision) {
  auto node = document.get(id);
  if (!node) return nullptr;

  // Children first: their revisions are part of this node's.
  std::vector<std::shared_ptr<Track> > children;
  std::vector<const doc::Node *> generators;
  uint64_t newest = node->revision;
  if (auto slot = node->children(tracknodes::kChildrenSlot)) {
    for (auto child_id : *slot) {
      auto child = document.get(child_id);
      if (!child) continue;
      if (child->type == tracknodes::kGeneratorType) {
        newest = std::max(newest, child->revision);
        generators.push_back(child);
        continue;
      }
      uint64_t child_revision = 0;
      auto compiled = compileNode(document, child_id, provider, &child_revision);
      newest = std::max(newest, child_revision);
      if (compiled) children.push_back(std::move(compiled));
    }
  }
  if (revision) *revision = newest;

  auto cached = entries_.find(id);
  if (cached != entries_.end() && cached->second.revision == newest) return cached->second.object;

  std::shared_ptr<Track> track = tracknodes::makeTrack(node->type);
  if (!track) return nullptr;
  if (auto iid = node->find(tracknodes::kIidKey)) {
    if (auto value = std::get_if<int64_t>(iid)) track->setInternalId(static_cast<int>(*value));
  }
  track->loadParameters(NodeParameterSource(document, id));
  if (node->type == "master") track->setId("master");

  if (auto * generic = dynamic_cast<GenericInstrument *>(track.get())) {
    for (auto generator : generators) {
      auto name = std::get_if<std::string>(generator->find("name"));
      auto value_text = std::get_if<std::string>(generator->find("value"));
      if (!name || !value_text) continue;
      auto value = std::strtof(value_text->c_str(), nullptr);
      if (auto generator_id = sf2GeneratorIdForName(*name)) generic->addGeneratorOverride(*generator_id, value);
      else generic->addUnknownGeneratorOverride(*name, value);
    }
  }
  if (provider) {
    if (auto * instrument = dynamic_cast<Instrument *>(track.get())) instrument->prepare(*provider);
  }
  for (auto & child : children) track->addChild(child);

  entries_[id] = Entry{ newest, track };
  return track;
}

std::shared_ptr<const CompiledTracks>
TrackCompiler::compile(const doc::Document & document, const Roots & roots, const InstrumentProvider * provider) {
  auto compiled = std::make_shared<CompiledTracks>();

  compiled->master = compileNode(document, roots.master, provider, nullptr);

  auto pool = std::make_shared<InstrumentPool>();
  pool->loadParameters(NodeParameterSource(document, roots.pool));
  if (auto node = document.get(roots.pool)) {
    if (auto slot = node->children(tracknodes::kChildrenSlot)) {
      for (auto child : *slot) {
        if (auto instrument = compileNode(document, child, provider, nullptr)) pool->addInstrument(std::move(instrument));
      }
    }
  }
  // The default kit is prepared once per `from`, with the provider at hand;
  // a later compile without one keeps it, and a pool that never had a
  // provider has none (the same as a song that was never loaded).
  auto from = pool->getDefaultKitFrom();
  if (provider) {
    if (!kit_ || kit_from_ != from) {
      pool->prepare(*provider);
      kit_ = pool->getDefaultKit();
      kit_from_ = from;
    } else {
      pool->setDefaultKit(kit_);
    }
  } else if (kit_ && kit_from_ == from) {
    pool->setDefaultKit(kit_);
  } else {
    pool->setDefaultKit(nullptr);
  }
  compiled->pool = std::move(pool);

  for (size_t slot = 0; slot < 2; slot++) {
    auto node = document.get(roots.bus[slot]);
    auto * descriptor = node ? findBusEffectDescriptor(node->type) : nullptr;
    if (!descriptor) descriptor = &findBusEffectDescriptor(slot == 0 ? BusEffectKind::Reverb : BusEffectKind::Delay);
    std::shared_ptr<BusEffect> effect = descriptor->factory(kPlaceholderBusSampleRate);
    if (node) effect->loadParameters(NodeParameterSource(document, roots.bus[slot]));
    compiled->bus[slot] = { descriptor->kind, std::move(effect) };
  }
  return compiled;
}
