#ifndef _TRACKCOMPILER_H_
#define _TRACKCOMPILER_H_

#include "CompiledTracks.h"
#include "../doc/Document.h"

#include <memory>
#include <unordered_map>
#include <vector>

class InstrumentProvider;
class GenericInstrument;

// Builds the immutable track objects from the document's nodes. Keeps what
// it built, keyed by node and the newest revision anywhere under it, so an
// edit rebuilds only the nodes that changed (and their ancestors) and shares
// the rest with the previous result.
class TrackCompiler {
 public:
  struct Roots {
    doc::NodeId master = doc::kNoNode;
    doc::NodeId pool = doc::kNoNode;
    doc::NodeId bus[2] = { doc::kNoNode, doc::kNoNode };
  };

  // Makes a detached node tree from `track` (its sub-tracks included) and
  // remembers the objects as what to use for those nodes, so a track added
  // as an object keeps its identity, its prepared instrument and everything
  // else it holds. The caller attaches the node and then calls
  // commitAdopted().
  doc::NodeId adopt(doc::Document & document, std::shared_ptr<Track> track);
  void commitAdopted(const doc::Document & document);

  // The newest revision under any of the roots; unchanged since the last
  // compile() means nothing needs compiling.
  static uint64_t stamp(const doc::Document & document, const Roots & roots);

  // `provider` prepares instruments that have to be built from their nodes
  // (those not adopted as objects) and the pool's default kit; null leaves
  // them unprepared.
  std::shared_ptr<const CompiledTracks> compile(const doc::Document & document, const Roots & roots, const InstrumentProvider * provider);

 private:
  struct Entry {
    uint64_t revision = 0;
    std::shared_ptr<Track> object;
  };
  std::shared_ptr<Track> compileNode(const doc::Document & document, doc::NodeId node, const InstrumentProvider * provider, uint64_t * revision);

  std::unordered_map<doc::NodeId, Entry> entries_;
  std::vector<std::pair<doc::NodeId, std::shared_ptr<Track> > > adopted_;
  std::shared_ptr<GenericInstrument> kit_;
  std::string kit_from_;
};

#endif
