#ifndef _TRACKNODES_H_
#define _TRACKNODES_H_

#include "Track.h"
#include "../doc/Document.h"
#include "../state/ParameterSource.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>

// How tracks, instruments and bus effects sit in the document. Each is a
// node whose type is the element name it has in a song file and whose string
// properties are exactly the attributes its loadParameters()/
// storeParameters() use, so the classes need no knowledge of the document
// and an attribute nothing recognizes survives untouched. A node's children
// are its sub-tracks (slot "children"); a GenericInstrument's <generator>
// elements are "generator" nodes with name/value properties.
namespace tracknodes {

constexpr const char * kChildrenSlot = "children";
constexpr const char * kIidKey = "#iid";   // the object's SongObject internal id
constexpr const char * kGeneratorType = "generator";

// A new Track for an element name ("track", "oscillator", ...), or null if
// the name is not a track kind. "master" is accepted too.
std::unique_ptr<Track> makeTrack(std::string_view element_name);

// A ParameterSource over a set of attribute strings.
class ParamBag : public ParameterSource {
 public:
  void set(const std::string & name, int value) override { values_[name] = std::to_string(value); }
  void set(const std::string & name, float value) override;
  void set(const std::string & name, const std::string & value) override { values_[name] = value; }
  bool has(const std::string & name) const override { return values_.count(name) != 0; }
  const std::map<std::string, std::string> & values() const { return values_; }

 protected:
  int getIntImpl(const std::string & name, int default_value) const override;
  float getFloatImpl(const std::string & name, float default_value) const override;
  std::string getTextImpl(const std::string & name, const std::string & default_value) const override;

 private:
  std::map<std::string, std::string> values_;
};

// Reads (and, on a detached node, writes) a node's properties as attributes.
class NodeParameterSource : public ParameterSource {
 public:
  NodeParameterSource(const doc::Document & document, doc::NodeId node) : document_(document), node_(node) { }
  NodeParameterSource(doc::Document & document, doc::NodeId node) : document_(document), writable_(&document), node_(node) { }

  void set(const std::string & name, int value) override { write(name, std::to_string(value)); }
  void set(const std::string & name, float value) override;
  void set(const std::string & name, const std::string & value) override { write(name, value); }
  bool has(const std::string & name) const override { return find(name) != nullptr; }

 protected:
  int getIntImpl(const std::string & name, int default_value) const override;
  float getFloatImpl(const std::string & name, float default_value) const override;
  std::string getTextImpl(const std::string & name, const std::string & default_value) const override;

 private:
  const std::string * find(const std::string & name) const;
  void write(const std::string & name, const std::string & value);
  const doc::Document & document_;
  doc::Document * writable_ = nullptr;
  doc::NodeId node_;
};

// Builds a detached node (with its sub-tracks) holding what `track` stores
// and remembers. The caller attaches it with Document::insertChild.
doc::NodeId trackToNode(doc::Document & document, const Track & track);

// The node's attributes as a bag, minus the bookkeeping property.
ParamBag nodeAttributes(const doc::Document & document, doc::NodeId node);

}  // namespace tracknodes

#endif
