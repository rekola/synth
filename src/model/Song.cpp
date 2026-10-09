#include "Song.h"

#include <cstdlib>
#include <set>

#include "../state/SongState.h"

#include "TrackCompiler.h"
#include "TrackNodes.h"

#include "InstrumentTrack.h"
#include "PercussionTrack.h"
#include "SampleTrack.h"
#include "SampleContent.h"
#include "Group.h"
#include "../audio/SampleFileLoader.h"
#include "../instruments/Arpeggiator.h"
#include "../instruments/Oscillator.h"
#include "../instruments/PadSynth.h"
#include "../instruments/Noise.h"
#include "../instruments/FM.h"
#include "../instruments/Additive.h"
#include "../instruments/GenericInstrument.h"

#include "../effects/Distortion.h"
#include "../effects/ResonantFilter.h"
#include "../effects/BiquadFilter.h"
#include "../effects/Chorus.h"
#include "../effects/Phaser.h"
#include "../effects/Tremolo.h"
#include "../effects/Amplifier.h"
#include "../effects/EnvelopeFilter.h"
#include "../effects/Compressor.h"
#include "../effects/TapeDegradation.h"

#include "../bus/BusEffectRegistry.h"
#include "../state/MemoryParameterSource.h"
#include "../instruments/SF2GeneratorTable.h"

#include <tinyxml2/tinyxml2.h>

#include "../util/constants.h"

#include <fmt/core.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_set>

using namespace std;
using namespace tinyxml2;

class XMLParameterSource : public ParameterSource {
public:
  XMLParameterSource(XMLElement * element) : element_(element) { }

  bool has(const std::string & name) const override { return element_->Attribute(name.c_str()) != 0; }

  void set(const std::string & name, int value) override { element_->SetAttribute(name.c_str(), value); }
  virtual void set(const std::string & name, float value) { element_->SetAttribute(name.c_str(), value); }
  virtual void set(const std::string & name, const std::string & value) { element_->SetAttribute(name.c_str(), value.c_str()); }

protected:
  int getIntImpl(const std::string & name, int default_value) const override {
    auto value = element_->Attribute(name.c_str());
    return value ? atoi(value) : default_value;
  }
  string getTextImpl(const std::string & name, const std::string & default_value) const override {
    auto value = element_->Attribute(name.c_str());
    return value ? value : default_value;
  }
  float getFloatImpl(const std::string & name, float default_value) const override {
    auto value = element_->Attribute(name.c_str());
    return value ? strtof(value, nullptr) : default_value;
  }

private:
  XMLElement * element_;
};

// A <note>/<command> element's own "track" attribute prefers a track's
// textual id (SongObject::getId(), e.g. "chords") over its raw internal
// id, matching how every other track reference in the file (e.g. a
// <track id="..."> element itself) already reads - falls back to the
// internal id, stringified, only for a track with no textual id of its
// own. resolveTrackReference() below is this function's own inverse.
static string trackReferenceText(const Song & song, int track_id) {
  auto track = song.getMasterTrack().getChildByInternalId(track_id);
  if (track && !track->getId().empty()) return track->getId();
  return to_string(track_id);
}

// The inverse of trackReferenceText() above: a track attribute may be
// either a track's own textual id or its raw internal id written as a
// decimal string (a track with no textual id of its own) - tried in that
// order, so a numeric-looking textual id (however unlikely) still wins
// over misreading it as an internal id. nullptr if neither resolves.
static const Track * resolveTrackReference(const Song & song, const char * text) {
  auto track = song.getMasterTrack().getChildById(text);
  if (track) return track;
  return song.getMasterTrack().getChildByInternalId(atoi(text));
}

string
sampleSidecarPath(const string & song_filename, const string & clip_id, int layer_index) {
  filesystem::path song_path(song_filename);
  auto stem = layer_index == 0 ? clip_id : clip_id + "_" + to_string(layer_index + 1);
  auto sample_path = song_path.parent_path() / (song_path.stem().string() + ".samples") / (stem + ".wav");
  return sample_path.string();
}

// A SampleTrack's own background bed's sidecar .wav stem/path - the same
// `<song-stem>.samples/` directory a clip's own sidecar uses
// (sampleSidecarPath() above), named by track id.
static string
sampleBackgroundStem(int track_id) {
  return "background_" + to_string(track_id);
}

static string
sampleBackgroundSidecarPath(const string & song_filename, int track_id) {
  filesystem::path song_path(song_filename);
  auto sample_path = song_path.parent_path() / (song_path.stem().string() + ".samples") / (sampleBackgroundStem(track_id) + ".wav");
  return sample_path.string();
}

// Parses a <pattern>'s own <note>/<command> children (and optional
// `length` attribute) directly into `pattern` - shared by the arrangement
// reader (Arrangement::patterns_by_track_id_'s own entry) and the clip reader
// below, which parse the identical <pattern> shape into two different
// kinds of owning container. false (with the malformed-command
// diagnostic already printed) on a corrupt <command>, matching both
// readers' own "bail the whole load out" contract on that.
static bool parsePatternContent(XMLElement & pattern_element, Pattern & pattern, Tuning tuning, const string & filename) {
  pattern.loadParameters(XMLParameterSource(&pattern_element));

  for (auto it = pattern_element.FirstChildElement("note"); it ; it = it->NextSiblingElement("note")) {
    auto row_text = it->Attribute("row");
    auto column_text = it->Attribute("column");
    auto velocity_text = it->Attribute("velocity");
    auto delay_text = it->Attribute("delay");

    auto value_text = it->GetText();
    if (!value_text) value_text = it->Attribute("value");

    if (value_text) {
      int row = row_text ? atoi(row_text) : 0;
      int start_column = column_text ? atoi(column_text) : 0;
      int velocity = velocity_text ? atoi(velocity_text) : constants::DEFAULT_VELOCITY;
      int delay = delay_text ? atoi(delay_text) : 0;

      auto notes = Note::createFromString(value_text, velocity, delay, tuning);
      for (int i = 0; i < static_cast<int>(notes.size()); i++) {
	pattern.setNote(row, start_column + i, notes[static_cast<size_t>(i)]);
      }
    }
  }

  for (auto it = pattern_element.FirstChildElement("command"); it; it = it->NextSiblingElement("command")) {
    auto row_text = it->Attribute("row");
    auto column_text = it->Attribute("column");
    auto data_text = it->Attribute("data");

    if (data_text) {
      int row = row_text ? atoi(row_text) : 0;
      int column = column_text ? atoi(column_text) : 0;
      // setData(), not the Command(string_view) constructor: untrusted
      // file data has to be actually detected as malformed here, not
      // silently fall back to a defined-but-wrong "----".
      Command command;
      if (!command.setData(data_text)) {
	fmt::print(stderr, "Malformed command \"{}\" at row {} in {}\n", data_text, row, filename);
	return false;
      }
      pattern.setCommand(row, column, command);
    }
  }
  return true;
}

static Tuning parse_tuning(string_view tuning_text, Tuning default_tuning = Tuning::EDO31) {
  if (tuning_text == "12edo") return Tuning::EDO12;
  else if (tuning_text == "31edo") return Tuning::EDO31;
  else if (tuning_text == "19edo") return Tuning::EDO19;
  else if (tuning_text == "53edo") return Tuning::EDO53;
  assert(0);
  return default_tuning;
}

// <generator name="..." value="..."/> children of an <instrument> element -
// song-authored SF2 generator overrides. Parsed *before* prepare() runs -
// prepare() is what actually applies these
// (GenericInstrument::prepare() -> Instrument::cloneWithOverrides()) and
// needs the full set already populated to decide whether cloning is even
// necessary. A recognized name resolves to its id and lands in
// generator_overrides_ (SF2GeneratorTable.h); an unrecognized one is kept,
// by name, in unknown_generator_overrides_ - preserved for a lossless
// round-trip but never applied by any backend (see that table's own doc
// comment for why an unrecognized name isn't simply rejected).
static void loadGeneratorOverrides(GenericInstrument & instrument, XMLElement & element) {
  for (auto it = element.FirstChildElement("generator"); it; it = it->NextSiblingElement("generator")) {
    auto name = it->Attribute("name");
    auto value_text = it->Attribute("value");
    if (!name || !value_text) continue;
    float value = strtof(value_text, nullptr);
    auto id = sf2GeneratorIdForName(name);
    if (id) instrument.addGeneratorOverride(*id, value);
    else instrument.addUnknownGeneratorOverride(name, value);
  }
}

static void storeGeneratorOverrides(const GenericInstrument & instrument, XMLDocument & doc, XMLElement * track_element) {
  for (auto & [id, value] : instrument.getGeneratorOverrides()) {
    auto name = sf2GeneratorNameForId(id);
    if (!name) continue; // every id here came from a known name in the first place
    auto generator_element = doc.NewElement("generator");
    generator_element->SetAttribute("name", name);
    generator_element->SetAttribute("value", value);
    track_element->InsertEndChild(generator_element);
  }
  for (auto & [name, value] : instrument.getUnknownGeneratorOverrides()) {
    auto generator_element = doc.NewElement("generator");
    generator_element->SetAttribute("name", name.c_str());
    generator_element->SetAttribute("value", value);
    track_element->InsertEndChild(generator_element);
  }
}

static std::unique_ptr<Track> parseChildTrack(XMLElement & element, const InstrumentProvider & provider) {
  // "master" is the tree parent, never an element a song names
  if (string_view(element.Name()) == "master") return std::unique_ptr<Track>(nullptr);
  auto track = tracknodes::makeTrack(element.Name());
  if (!track) return std::unique_ptr<Track>(nullptr);

  track->loadParameters(XMLParameterSource(&element));

  auto generic_instrument = dynamic_cast<GenericInstrument *>(track.get());
  if (generic_instrument) {
    loadGeneratorOverrides(*generic_instrument, element);
  }

  auto instrument = dynamic_cast<Instrument *>(track.get());
  if (instrument) {
    instrument->prepare(provider);
  }

  for (auto it = element.FirstChildElement(); it ; it = it->NextSiblingElement() ) {
    if (string_view(it->Name()) == "lane") continue; // legacy per-track drum list, no longer used
    if (string_view(it->Name()) == "generator") continue; // data, not a nested track - handled above
    auto child = parseChildTrack(*it, provider);
    if (!child) return std::unique_ptr<Track>(nullptr);
    track->addChild(std::move(child));
  }

  // A leaf instrument has nothing to do with children (they used to be FM
  // modulators).
  if (instrument && !track->getChildren().empty()) {
    return std::unique_ptr<Track>(nullptr);
  }

  return track;
}

// Writes <note>/<command> children into `pattern_element` for every row
// `pattern` actually has content on, in ascending row order - the write
// side of parsePatternContent() above, shared the same way by the
// arrangement writer and the clip writer below. Reads the raw row->note-
// columns map directly (sorted, since it's an unordered_map) rather than
// looping some external row bound.
static void storePatternContent(XMLDocument & doc, XMLElement * pattern_element, const Pattern & pattern, Tuning tuning) {
  XMLParameterSource pattern_parameters(pattern_element);
  pattern.storeParameters(pattern_parameters);

  vector<unsigned short> rows;
  for (auto & [ row, nv ] : pattern.getNotesByRow()) rows.push_back(row);
  sort(rows.begin(), rows.end());
  for (auto row : rows) {
    auto & nv = pattern.getNotesByRow().at(row);
    // TODO: check if velocity and delay are same, and store notes in single element
    for (size_t col = 0; col < nv.size(); col++) {
      auto & note = nv[col];
      // An undefined note (Note::isDefined() false) carries no data worth
      // persisting, and its toString() text ("···") isn't a value
      // Note::stringToKey() can parse back on load - only ever meant for
      // display. A column can still hold one of these as a mid-vector gap
      // (e.g. a chord's lower note deleted while a higher one stays
      // defined - Pattern::deleteNote only trims trailing entries), so
      // skip it here rather than assuming the vector itself never holds one.
      if (!note.isDefined()) continue;
      auto note_text = note.toString(tuning);
      auto note_element = doc.NewElement("note");
      note_element->SetAttribute("row", static_cast<int>(row));
      if (col > 0) note_element->SetAttribute("column", col);
      if (note.getVelocity() != constants::DEFAULT_VELOCITY) note_element->SetAttribute("velocity", note.getVelocity());
      if (note.getDelay() > 0) note_element->SetAttribute("delay", note.getDelay());
      note_element->SetText(note_text.c_str());
      pattern_element->InsertEndChild(note_element);
    }
  }

  for (auto & [ row, cv ] : pattern.getCommandsByRow()) {
    for (size_t col = 0; col < cv.size(); col++) {
      auto & command = cv[col];
      // A mid-vector gap (one column's own command cleared while a
      // higher-numbered one stays defined) is possible the same way it is
      // for notes - see storePatternContent()'s own note-writing loop
      // above for why this is skipped rather than assumed unreachable.
      if (!command.isDefined()) continue;
      auto data = to_string(command);
      auto command_element = doc.NewElement("command");
      command_element->SetAttribute("row", static_cast<int>(row));
      if (col > 0) command_element->SetAttribute("column", col);
      command_element->SetAttribute("data", data.c_str());
      pattern_element->InsertEndChild(command_element);
    }
  }
}

static void storeChildTrack(const Track & track, XMLDocument & doc, XMLElement * target_element) {
  auto name = track.getElementName();
  auto track_element = doc.NewElement(name);
  XMLParameterSource parameters(track_element);
  track.storeParameters(parameters);

  for (auto & child : track.getChildren()) {
    storeChildTrack(*child, doc, track_element);
  }

  auto generic_instrument = dynamic_cast<const GenericInstrument *>(&track);
  if (generic_instrument) {
    storeGeneratorOverrides(*generic_instrument, doc, track_element);
  }

  target_element->InsertEndChild(track_element);
}

// Every InstrumentTrack in the tree (Group/Effect wrappers included - a pool
// index has no notion of "root track only"), with its new pool index after
// slot `removed_index` has just gone: pointing past it shifts down, pointing
// at it is unassigned. Only the tracks that change are listed.
static void collectInstrumentIdChanges(const Track & track, int removed_index, std::vector<std::pair<int, int> > & changes) {
  if (auto * instrument_track = dynamic_cast<const InstrumentTrack *>(&track)) {
    auto id = instrument_track->getInstrumentId();
    if (id == removed_index) changes.emplace_back(track.getInternalId(), -1);
    else if (id > removed_index) changes.emplace_back(track.getInternalId(), id - 1);
  }
  for (auto & child : track.getChildren()) collectInstrumentIdChanges(*child, removed_index, changes);
}

void
Song::removeInstrument(int index) {
  auto & instruments = getInstrumentPool().getInstruments();
  if (index < 0 || index >= static_cast<int>(instruments.size())) return;
  Edit edit(*this, "remove instrument");
  std::vector<std::pair<int, int> > changes;
  collectInstrumentIdChanges(getMasterTrack(), index, changes);
  doc_->removeChild(poolNode(), tracknodes::kChildrenSlot, static_cast<size_t>(index));
  for (auto [ track_id, new_id ] : changes) {
    editTrack(track_id, [&](Track & track) { static_cast<InstrumentTrack &>(track).setInstrumentId(new_id); });
  }
}

void
Song::addInstrument(std::unique_ptr<Track> instrument) {
  Edit edit(*this, "add instrument");
  std::shared_ptr<Track> shared = std::move(instrument);
  auto node = compiler_->adopt(*doc_, shared);
  auto pool = poolNode();
  auto slot = doc_->get(pool)->children(tracknodes::kChildrenSlot);
  doc_->insertChild(pool, tracknodes::kChildrenSlot, slot ? slot->size() : 0, node);
  compiler_->commitAdopted(*doc_);
}

doc::NodeId
Song::masterNode() const {
  auto slot = doc_->get(doc_->root())->children("master");
  return slot && !slot->empty() ? slot->front() : doc::kNoNode;
}

doc::NodeId
Song::poolNode() const {
  auto slot = doc_->get(doc_->root())->children("instruments");
  return slot && !slot->empty() ? slot->front() : doc::kNoNode;
}

doc::NodeId
Song::busNode(int slot) const {
  auto nodes = doc_->get(doc_->root())->children("bus");
  return nodes && nodes->size() > static_cast<size_t>(slot) ? (*nodes)[static_cast<size_t>(slot)] : doc::kNoNode;
}

// The node of the track whose internal id is `track_id` (the master's
// included), searching its subtree.
static doc::NodeId findTrackNode(const doc::Document & document, doc::NodeId node, int track_id) {
  auto n = document.get(node);
  if (!n) return doc::kNoNode;
  if (auto iid = n->find(tracknodes::kIidKey)) {
    if (auto value = std::get_if<int64_t>(iid); value && *value == track_id) return node;
  }
  if (auto children = n->children(tracknodes::kChildrenSlot)) {
    for (auto child : *children) {
      if (auto found = findTrackNode(document, child, track_id); found != doc::kNoNode) return found;
    }
  }
  return doc::kNoNode;
}

doc::NodeId
Song::trackNode(int track_id) const {
  return findTrackNode(*doc_, masterNode(), track_id);
}

static void collectTrackIds(const doc::Document & document, doc::NodeId node, std::set<std::string> & ids) {
  auto n = document.get(node);
  if (!n) return;
  if (auto id = n->find("id")) {
    if (auto text = std::get_if<std::string>(id)) ids.insert(*text);
  }
  if (auto children = n->children(tracknodes::kChildrenSlot)) {
    for (auto child : *children) collectTrackIds(document, child, ids);
  }
}

std::string
Song::generateUniqueTrackId() const {
  std::set<std::string> taken;
  collectTrackIds(*doc_, masterNode(), taken);
  for (int n = 1; ; n++) {
    auto candidate = "track" + std::to_string(n);
    if (!taken.count(candidate)) return candidate;
  }
}

const Track &
Song::addTrack(std::unique_ptr<Track> track, int after_track_id) {
  Edit edit(*this, "add track");
  if (track->getId().empty()) track->setId(generateUniqueTrackId());
  std::shared_ptr<Track> shared = std::move(track);

  auto parent = masterNode();
  std::string slot = tracknodes::kChildrenSlot;
  size_t index = doc_->get(parent)->children(slot) ? doc_->get(parent)->children(slot)->size() : 0;
  if (after_track_id >= 0) {
    if (auto after = trackNode(after_track_id); after != doc::kNoNode && after != parent) {
      auto after_node = doc_->get(after);
      parent = after_node->parent;
      slot = after_node->parent_slot;
      auto siblings = doc_->get(parent)->children(slot);
      index = static_cast<size_t>(std::find(siblings->begin(), siblings->end(), after) - siblings->begin()) + 1;
    }
  }
  auto node = compiler_->adopt(*doc_, shared);
  doc_->insertChild(parent, slot, index, node);
  compiler_->commitAdopted(*doc_);
  return *shared;
}

bool
Song::removeTrack(int id) {
  Edit edit(*this, "remove track");
  auto node = trackNode(id);
  if (node == doc::kNoNode || node == masterNode()) {
    edit.discard();
    return false;
  }
  auto n = doc_->get(node);
  auto parent = n->parent;
  auto slot = n->parent_slot;
  auto siblings = doc_->get(parent)->children(slot);
  auto index = static_cast<size_t>(std::find(siblings->begin(), siblings->end(), node) - siblings->begin());
  doc_->removeChild(parent, slot, index);
  return true;
}

bool
Song::editTrack(int track_id, const std::function<void(Track &)> & edit) {
  auto node = trackNode(track_id);
  if (node == doc::kNoNode) return false;
  auto n = doc_->get(node);

  // A scratch copy built from the node, changed by `edit`; what differs in
  // the attributes it stores is what gets written back. Attributes nothing
  // here recognizes are never in either bag, so they stay as they were.
  auto scratch = tracknodes::makeTrack(n->type);
  if (!scratch) return false;
  scratch->loadParameters(tracknodes::NodeParameterSource(*doc_, node));
  if (node == masterNode()) scratch->setId("master");
  tracknodes::ParamBag before;
  scratch->storeParameters(before);
  edit(*scratch);
  tracknodes::ParamBag after;
  scratch->storeParameters(after);
  if (before.values() == after.values()) return true;

  Edit scope(*this, "edit track");
  for (auto & [ key, value ] : after.values()) {
    auto old = before.values().find(key);
    if (old == before.values().end() || old->second != value) doc_->setProperty(node, key, doc::Value(value));
  }
  for (auto & [ key, value ] : before.values()) {
    if (!after.values().count(key)) doc_->setProperty(node, key, doc::Value());
  }
  return true;
}

void
Song::compileTracks(const InstrumentProvider * provider) {
  TrackCompiler::Roots roots;
  roots.master = masterNode();
  roots.pool = poolNode();
  roots.bus[0] = busNode(0);
  roots.bus[1] = busNode(1);
  tracks_ = compiler_->compile(*doc_, roots, provider);
  tracks_stamp_ = TrackCompiler::stamp(*doc_, roots);
}

void
Song::recompileTracksIfChanged() {
  TrackCompiler::Roots roots;
  roots.master = masterNode();
  roots.pool = poolNode();
  roots.bus[0] = busNode(0);
  roots.bus[1] = busNode(1);
  if (TrackCompiler::stamp(*doc_, roots) != tracks_stamp_) compileTracks(nullptr);
}

SongScalars
Song::scalars() const {
  SongScalars out;
  out.tempo = getTempo();
  out.swing = getSwing();
  out.time_signature = getTimeSignature();
  out.running_bars = getRunningBars();
  out.tuning = getTuning();
  out.ear_height = getEarHeight();
  out.floor_reflection_enabled = getFloorReflectionEnabled();
  out.floor_reflection_strength = getFloorReflectionStrength();
  out.ground_absorption = getGroundAbsorption();
  return out;
}

void
Song::publishContent() const {
  content_publisher_->publish(compileContent());
}

std::unique_ptr<PlaybackContent>
Song::compileContent() const {
  auto content = std::make_unique<PlaybackContent>();
  content->scalars = scalars();
  content->tracks = tracks_;
  content->arrangement = getArrangement().toArrangement();
  for (auto track_id : clipTrackIds()) {
    auto & compiled = content->clips_by_track[track_id];
    for (auto clip : getClips(track_id)) compiled.push_back(clip.toClip());
  }
  return content;
}

std::vector<int>
Song::clipTrackIds() const {
  std::vector<int> ids;
  const std::string prefix = "clips:";
  for (auto & slot : doc_->get(doc_->root())->slots) {
    if (slot.name.compare(0, prefix.size(), prefix) == 0) ids.push_back(std::atoi(slot.name.c_str() + prefix.size()));
  }
  return ids;
}

int
Song::getUsedSceneCount() const {
  size_t used = 0;
  for (auto track_id : clipTrackIds()) {
    auto clips = getClips(track_id);
    for (size_t i = clips.size(); i > used; i--) {
      if (!clips[i - 1].isEmpty()) {
        used = i;
        break;
      }
    }
  }
  return static_cast<int>(used);
}

ClipView
Song::addClip(Clip clip) {
  Edit edit(*this, "add clip");
  if (clip.getId().empty()) clip.setId(generateUniqueClipId());
  auto node = ClipView::create(context(), clip.getLeafTrackId());
  ClipView view(context(), node);
  view.assign(clip); // off to the side, so not history
  doc_->insertChild(doc_->root(), ClipList::slotName(clip.getLeafTrackId()), getClips(clip.getLeafTrackId()).size(), node);
  return view;
}

ClipView
Song::ensureClipAt(int track_id, int index) {
  Edit edit(*this, "ensure clip slot");
  auto clips = getClips(track_id);
  while (static_cast<int>(clips.size()) <= index) {
    doc_->insertChild(doc_->root(), ClipList::slotName(track_id), clips.size(), ClipView::create(context(), track_id));
  }
  return clips[static_cast<size_t>(index)];
}

std::string
Song::generateUniqueClipId() const {
  std::set<std::string> taken;
  for (auto track_id : clipTrackIds()) {
    for (auto clip : getClips(track_id)) taken.insert(clip.getId());
  }
  for (int n = 1; ; n++) {
    auto candidate = "clip" + std::to_string(n);
    if (!taken.count(candidate)) return candidate;
  }
}

bool
Song::publishedContentIsCurrent() const {
  auto content = ContentPublisher::Reader(*content_publisher_);
  auto fresh = compileContent();
  return contentDigest(content->scalars, content->arrangement, content->clips_by_track) == contentDigest(fresh->scalars, fresh->arrangement, fresh->clips_by_track);
}

// Sample rate used to construct Song's own bus-slot BusEffect instances
// (Song::bus_slot_a_/bus_slot_b_) - these exist purely to own/(de)serialize
// their own parameters (see Song.h's own doc comment on getBusSlot()) and
// are never process()'d, so the exact value doesn't matter for correctness;
// SongState::initialize() constructs the real, correctly-sample-rated
// instances the audio thread actually uses.
static constexpr int kPlaceholderBusSampleRate = 44100;

// <bus> child elements are resolved to a slot by document order alone (no
// disambiguating attribute - see Song.h's own doc comment and the
// project-file plan): the first child is slot 0 (A), the second is slot 1
// (B). An unrecognized element name falls back to that slot's own default
// type (A -> reverb, B -> delay), never to None - an unrecognized name is
// a corrupted/future-version reference, which should degrade to "play
// something sensible," not go silent.
static void parseBusSlot(Song & song, int slot, XMLElement & element) {
  auto * descriptor = findBusEffectDescriptor(element.Name());
  if (!descriptor) {
    // TODO: route through this codebase's non-fatal load-warning channel
    // (see Controller.cpp) once one exists for Song::open() itself.
    descriptor = &findBusEffectDescriptor(slot == 0 ? BusEffectKind::Reverb : BusEffectKind::Delay);
  }
  std::unique_ptr<BusEffect> effect = descriptor->factory(kPlaceholderBusSampleRate);
  effect->loadParameters(XMLParameterSource(&element));
  song.setBusSlotKind(slot, descriptor->kind, effect.get());
}

// True when `slot` is still exactly the compiled-in default for its
// position (`default_kind`, with every parameter also still at that
// type's own default) - checked generically, via storeParameters()'s own
// deviation-only logic, rather than needing per-type knowledge here of
// what "default" means for every possible parameter.
static bool busSlotIsDefault(const Song & song, int slot, BusEffectKind default_kind) {
  if (song.getBusSlotKind(slot) != default_kind) return false;
  MemoryParameterSource params;
  song.getBusSlot(slot).storeParameters(params);
  return params.isEmpty();
}

static void storeBusSlotChild(const Song & song, int slot, XMLDocument & doc, XMLElement * bus) {
  auto & descriptor = findBusEffectDescriptor(song.getBusSlotKind(slot));
  auto element = doc.NewElement(descriptor.xmlName);
  XMLParameterSource params(element);
  song.getBusSlot(slot).storeParameters(params);
  bus->InsertEndChild(element);
}

// Writes <bus> only when the resolved 2-slot configuration actually
// deviates from the compiled-in default (A = reverb, B = delay, both at
// their own defaults) - the "default config stores nothing" rule, applied
// once to the whole bus rather than per slot, since <bus>'s presence is a
// full override of the default bus, not a per-slot merge with it (see
// Song.h's own doc comment). When something does need writing: slot 0 is
// always present (as <none/> if genuinely empty, otherwise as its own
// element, even bare if only slot 1 actually deviates - an unavoidable
// placeholder under "presence overrides, doesn't merge" - a 1-child <bus>
// always means "slot 0 only, slot 1 is empty" so there's no way to omit a
// non-empty slot 0); slot 1 is omitted only when it's genuinely empty
// (trailing-omission means empty on load, so relying on it here is always
// safe) - written explicitly, even bare, whenever it isn't, including
// when it's still the default delay (which must not be confused with the
// 1-child shorthand for "empty").
static void storeBusConfig(const Song & song, XMLDocument & doc, XMLElement * root) {
  bool a_default = busSlotIsDefault(song, 0, BusEffectKind::Reverb);
  bool b_default = busSlotIsDefault(song, 1, BusEffectKind::Delay);
  if (a_default && b_default) return;

  auto bus = doc.NewElement("bus");
  root->InsertEndChild(bus);

  bool a_none = song.getBusSlotKind(0) == BusEffectKind::None;
  bool b_none = song.getBusSlotKind(1) == BusEffectKind::None;
  if (a_none && b_none) return; // <bus/> - both slots empty

  storeBusSlotChild(song, 0, doc, bus);
  if (!b_none) storeBusSlotChild(song, 1, doc, bus);
}

static_assert(static_cast<int>(Tuning::EDO31) == 3, "songschema::kTuning's default is Tuning::EDO31");
static_assert(static_cast<int>(Scale::NONE) == 0, "songschema::kScale's default is Scale::NONE");

Song::Song(Tuning tuning, short key) {
  // Every transaction is replayed against a copy to check that its undo
  // record restores the tree; slow, for tests and debugging.
  static const bool verify = std::getenv("SYNTH_VERIFY_DOCUMENT") != nullptr;
  doc_->setVerify(verify);
  // The construction arguments are the starting state, not an edit.
  doc::set(*doc_, doc_->root(), songschema::kTuning, static_cast<int>(tuning));
  doc::set(*doc_, doc_->root(), songschema::kKey, static_cast<int>(key));
  compiler_ = std::make_shared<TrackCompiler>();
  auto master = std::make_shared<MasterTrack>();
  master->loadParameters(MemoryParameterSource()); // the type's own defaults (collapsed)
  master->setId("master");
  auto master_node = compiler_->adopt(*doc_, master);
  doc_->insertChild(doc_->root(), "master", 0, master_node);
  doc_->insertChild(doc_->root(), "instruments", 0, doc_->create("instruments"));
  doc_->insertChild(doc_->root(), "bus", 0, doc_->create(findBusEffectDescriptor(BusEffectKind::Reverb).xmlName));
  doc_->insertChild(doc_->root(), "bus", 1, doc_->create(findBusEffectDescriptor(BusEffectKind::Delay).xmlName));
  compiler_->commitAdopted(*doc_);
  compileTracks();
  arrangement_node_ = ArrangementView::create(*doc_);
  doc_->insertChild(doc_->root(), scoreschema::kArrangementSlot, 0, arrangement_node_);
  doc_->clearJournal();
}

void
Song::setBusSlotKind(int slot, BusEffectKind kind, const BusEffect * parameters) {
  slot = slot == 0 ? 0 : 1;
  Edit edit(*this, "set bus effect");
  auto node = doc_->create(findBusEffectDescriptor(kind).xmlName);
  if (parameters) {
    tracknodes::ParamBag bag;
    parameters->storeParameters(bag);
    for (auto & [ key, value ] : bag.values()) doc_->setProperty(node, key, doc::Value(value));
  }
  doc_->removeChild(doc_->root(), "bus", static_cast<size_t>(slot));
  doc_->insertChild(doc_->root(), "bus", static_cast<size_t>(slot), node);
}

bool
Song::open(const std::string & filename, const InstrumentProvider & provider) {
  // setlocale(..., nullptr) returns a pointer into glibc's own internal
  // locale-name buffer, not a caller-owned string - it's only valid until
  // the very next setlocale() call, so it must be copied into a std::string
  // here rather than kept as a const char*: the setlocale(LC_ALL, "C") call
  // right below would otherwise overwrite the very buffer this points to,
  // so the restore at the end of this function would hand setlocale()
  // garbage instead of the original locale name.
  auto oldLocale = std::string{ setlocale(LC_ALL, nullptr) };
  setlocale(LC_ALL, "C");
  
  XMLDocument doc;
  if (doc.LoadFile(filename.c_str()) != 0) {
    // Covers both "file doesn't exist" and "file exists but isn't valid
    // XML" - tinyxml2's own ErrorStr() distinguishes them (and gives a
    // line number for the latter), which a caller-side blanket "file not
    // found" message can't - see main.cpp's own callers of openSong().
    fmt::print(stderr, "Could not load {}: {}\n", filename, doc.ErrorStr());
    // Set the old locale before exiting
    setlocale(LC_ALL, oldLocale.c_str());
    return false;
  }

  auto song = doc.FirstChildElement("song");
  if (song) {
    loadParameters(XMLParameterSource(song)); // resets both bus slots to their compiled defaults

    // <bus>'s presence fully overrides the default bus (A = reverb,
    // B = delay) rather than merging with it - if absent, both slots
    // stay exactly as loadParameters() just reset them to. Children are
    // resolved to a slot by document order alone (see parseBusSlot()) -
    // a trailing missing child leaves that slot at None (empty), which
    // parseBusSlot() never sees since it's simply never called for it.
    auto bus = song->FirstChildElement("bus");
    if (bus) {
      setBusSlotKind(0, BusEffectKind::None);
      setBusSlotKind(1, BusEffectKind::None);
      int slot = 0;
      for (auto it = bus->FirstChildElement(); it && slot < 2; it = it->NextSiblingElement(), slot++) {
        parseBusSlot(*this, slot, *it);
      }
    }

    auto instruments = song->FirstChildElement("instruments");
    if (instruments) {
      {
	auto from = XMLParameterSource(instruments).get<std::string>("from");
	Edit pool_edit(*this, "set default kit");
	doc_->setProperty(poolNode(), "from", from.empty() ? doc::Value() : doc::Value(from));
      }
      for (auto it = instruments->FirstChildElement(); it; it = it->NextSiblingElement() ) {
	auto instrument = parseChildTrack(*it, provider);
	// Unrecognized (or, recursively, containing an unrecognized child)
	// is fatal to the whole load, not silently dropped - makeTrack()
	// returns null for an unknown element name, which is caught here.
	if (!instrument) {
	  fmt::print(stderr, "Unrecognized or malformed <{}> in {}\n", it->Name(), filename);
	  setlocale(LC_ALL, oldLocale.c_str());
	  return false;
	}
	addInstrument(move(instrument));
      }
    }

    auto tracks = song->FirstChildElement("tracks");
    if (tracks) {
      editTrack(getMasterTrack().getInternalId(), [&](Track & master) {
	master.loadParameters(XMLParameterSource(tracks));
	master.setId("master");
      });
      for (auto it = tracks->FirstChildElement(); it ; it = it->NextSiblingElement() ) {
	auto track = parseChildTrack(*it, provider);
	// Same "fatal, not silently dropped" rule as the <instruments> loop
	// above - a song missing a whole track because its element name
	// wasn't recognized must fail to load, not open looking complete.
	if (!track) {
	  fmt::print(stderr, "Unrecognized or malformed <{}> in {}\n", it->Name(), filename);
	  setlocale(LC_ALL, oldLocale.c_str());
	  return false;
	}
	addTrack(move(track));
      }
    }
    
    if (auto locators = song->FirstChildElement("locators")) {
      for (auto it = locators->FirstChildElement("locator"); it; it = it->NextSiblingElement("locator")) {
        auto row_text = it->Attribute("row");
        auto name = it->GetText();
        if (row_text && name) setLocator(atoi(row_text), name);
      }
    }

    clearScenes();
    if (auto scenes = song->FirstChildElement("scenes")) {
      int scene_index = 0;
      for (auto it = scenes->FirstChildElement("scene"); it; it = it->NextSiblingElement("scene"), scene_index++) {
        auto name = it->Attribute("name");
        setSceneName(scene_index, name ? name : "");
        setSceneTempo(scene_index, std::max(it->IntAttribute("tempo", 0), 0));
        int numerator = 0, denominator = 0;
        if (auto text = it->Attribute("timeSignature"); text && sscanf(text, "%d/%d", &numerator, &denominator) == 2 && numerator > 0 && numerator <= 32 && scenename::validDenominator(denominator)) {
          setSceneTimeSignature(scene_index, {numerator, denominator});
        }
      }
    }

    // Song's own flat, per-track clip list (Song.h's own getClips()
    // comment), read before <arrangement>, whose instances refer to it.
    // One <trackClips> per track that has any clips at all, grouping that
    // track's own <clip> children in order rather than repeating a track
    // reference on every single <clip>. Each <clip> holds its own
    // name/loop/length (Clip::loadParameters()) plus a nested <pattern>
    // for its leaf track's own note/command content - the same shape the
    // arrangement's own inline <pattern> uses (parsePatternContent()
    // above), just with no name/loop/length of its own.
    auto clips_element = song->FirstChildElement("clips");
    if (clips_element) {
      for (auto track_it = clips_element->FirstChildElement("trackClips"); track_it; track_it = track_it->NextSiblingElement("trackClips")) {
	auto track_text = track_it->Attribute("track");
	auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	if (!track) continue;

	for (auto it = track_it->FirstChildElement("clip"); it ; it = it->NextSiblingElement("clip")) {
	  Clip clip(track->getInternalId());
	  auto sample_element = it->FirstChildElement("sample");
	  if (sample_element) {
	    // One or more layers (Clip.h's own sample_layers_ comment) - each a
	    // sibling <sample> in take order, layer 0 first; an older
	    // single-<sample> file is just the size-1 case of the same loop.
	    for (; sample_element; sample_element = sample_element->NextSiblingElement("sample")) {
	      // A named, exact file reference - missing/unreadable is fatal to
	      // the whole load, the same way a malformed <pattern> already is
	      // below, not silently skipped the way an unresolved instrument
	      // name falls back to a generic default elsewhere: the artist
	      // named one specific file, not something to resolve loosely.
	      auto file_attr = sample_element->Attribute("file");
	      if (!file_attr) {
		fmt::print(stderr, "Malformed <sample> (missing file attribute) in {}\n", filename);
		setlocale(LC_ALL, oldLocale.c_str());
		return false;
	      }
	      auto sample_path = filesystem::path(filename).parent_path() / file_attr;
	      auto loaded = loadMonoSample(sample_path.string());
	      if (!loaded.buffer) {
		fmt::print(stderr, "Could not load sample \"{}\" referenced by clip in {}\n", sample_path.string(), filename);
		setlocale(LC_ALL, oldLocale.c_str());
		return false;
	      }
	      auto & content = clip.getSampleLayers().empty() ? clip.getSampleContent() : clip.addSampleLayer();
	      content.setBuffer(loaded.buffer);
	      content.setNativeSampleRate(loaded.rate);
	      content.loadParameters(XMLParameterSource(sample_element));
	    }
	  } else {
	    auto pattern_element = it->FirstChildElement("pattern");
	    if (pattern_element && !parsePatternContent(*pattern_element, clip.getLeafPattern(), getTuningForTrack(*track), filename)) {
	      setlocale(LC_ALL, oldLocale.c_str());
	      return false;
	    }
	  }
	  clip.loadParameters(XMLParameterSource(it));
	  addClip(std::move(clip));
	}
      }
    }

    // <arrangement>: the one timeline - each track's inline <pattern>,
    // its placed clips (<instances>) and a SampleTrack's background bed.
    if (auto arrangement = song->FirstChildElement("arrangement")) {
      auto timeline = getArrangement();
      for (auto it = arrangement->FirstChildElement("pattern"); it ; it = it->NextSiblingElement("pattern")) {
	auto track_text = it->Attribute("track");
	auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	if (!track) continue;

	Pattern pattern;
	if (!parsePatternContent(*it, pattern, getTuningForTrack(*track), filename)) {
	  setlocale(LC_ALL, oldLocale.c_str());
	  return false;
	}
	timeline.setPatternForTrack(track->getInternalId(), pattern);
      }

      for (auto it = arrangement->FirstChildElement("instances"); it ; it = it->NextSiblingElement("instances")) {
	auto track_text = it->Attribute("track");
	auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	if (!track) continue;

	for (auto it2 = it->FirstChildElement("instance"); it2 ; it2 = it2->NextSiblingElement("instance")) {
	  auto row_text = it2->Attribute("row");
	  auto value_text = it2->GetText();
	  if (row_text && value_text) timeline.setInstance(track->getInternalId(), atoi(row_text), value_text);
	}
      }

      // A background bed is never trimmed or tempo-stretched, so it has no
      // in/out/originalTempo of its own, unlike a clip's <sample>.
      for (auto it = arrangement->FirstChildElement("sampleBackground"); it ; it = it->NextSiblingElement("sampleBackground")) {
	auto track_text = it->Attribute("track");
	auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	if (!track) continue;

	auto file_attr = it->Attribute("file");
	if (!file_attr) {
	  fmt::print(stderr, "Malformed <sampleBackground> (missing file attribute) in {}\n", filename);
	  setlocale(LC_ALL, oldLocale.c_str());
	  return false;
	}
	auto sample_path = filesystem::path(filename).parent_path() / file_attr;
	auto loaded = loadMonoSample(sample_path.string());
	if (!loaded.buffer) {
	  fmt::print(stderr, "Could not load sample \"{}\" referenced by <sampleBackground> in {}\n", sample_path.string(), filename);
	  setlocale(LC_ALL, oldLocale.c_str());
	  return false;
	}
	SampleContent content;
	content.setBuffer(loaded.buffer);
	content.setNativeSampleRate(loaded.rate);
	timeline.setSampleBackground(track->getInternalId(), content);
      }
    }
  }

  // Set the old locale before exiting
  setlocale(LC_ALL, oldLocale.c_str());
  compileTracks(&provider); // prepares the default kit; instruments were prepared as they were parsed
  doc_->clearJournal(); // opening a file is not an edit
  if (content_published_mode_) publishContent();
  return true;
}

void
Song::save(const std::string & filename) const {
  // Copied into a std::string, not kept as the raw const char* setlocale()
  // returns: that pointer is only valid until the next setlocale() call, so
  // the "C" switch below would invalidate it before the restore at the end
  // of this function gets to use it.
  auto oldLocale = std::string{ setlocale(LC_ALL, nullptr) };
  setlocale(LC_ALL, "C");
 
  XMLDocument doc;
  doc.InsertEndChild(doc.NewDeclaration());
    
  auto root = doc.NewElement("song");
  XMLParameterSource song_parameters(root);
  storeParameters(song_parameters);
  doc.InsertEndChild(root);

  storeBusConfig(*this, doc, root);

  auto instruments = doc.NewElement("instruments");
  XMLParameterSource instruments_parameters(instruments);
  getInstrumentPool().storeParameters(instruments_parameters);
  root->InsertEndChild(instruments);

  auto tracks = doc.NewElement("tracks");
  XMLParameterSource tracks_parameters(tracks);
  getMasterTrack().storeParameters(tracks_parameters);
  root->InsertEndChild(tracks);

  // Sibling of <tracks>/<arrangement> - Song's own flat, per-track clip list
  // (Song.h's own getClips() comment). Omitted entirely (not written as an
  // empty <clips/>) when there's nothing in it, same "default/empty state
  // stores nothing" rule storeBusConfig() already follows - most songs
  // never use this feature at all, and a spurious empty element on every
  // one of them would just be diff noise. Walked via getRootTrackIds()
  // (tree order), not clips_by_track_ directly, for the same
  // deterministic-output reason storeChildTrack() walks the tree itself
  // rather than some other, unordered collection. One <trackClips> per
  // track, grouping that track's own <clip> children rather than
  // repeating a track reference on every single one.
  auto compiled = compileContent();
  bool has_clips = std::any_of(compiled->clips_by_track.begin(), compiled->clips_by_track.end(),
    [](auto & entry) { return !entry.second.empty(); });
  if (has_clips) {
    auto clips_element = doc.NewElement("clips");
    root->InsertEndChild(clips_element);
    for (auto track_id : getRootTrackIds()) {
      auto & clips = compiled->getClips(track_id);
      if (clips.empty()) continue;

      auto track = getMasterTrack().getChildByInternalId(track_id);
      assert(track);
      if (!track) continue;
      auto track_tuning = getTuningForTrack(*track);
      auto track_ref = trackReferenceText(*this, track_id);

      auto track_clips_element = doc.NewElement("trackClips");
      track_clips_element->SetAttribute("track", track_ref.c_str());
      clips_element->InsertEndChild(track_clips_element);

      for (auto & clip : clips) {
	auto clip_element = doc.NewElement("clip");
	XMLParameterSource clip_parameters(clip_element);
	clip.storeParameters(clip_parameters);

	if (clip.hasSample()) {
	  // One <sample> child per layer, in take order (Clip.h's own
	  // sample_layers_ comment) - a single-layer clip (the overwhelming
	  // majority) writes exactly the one <sample> element a pre-overdub
	  // file already had, so this is backward-compatible with every
	  // existing song on disk.
	  auto & layers = clip.getSampleLayers();
	  for (size_t layer_index = 0; layer_index < layers.size(); layer_index++) {
	    auto & content = layers[layer_index];
	    if (!content.getBuffer()) continue;
	    filesystem::path sample_path(sampleSidecarPath(filename, clip.getId(), static_cast<int>(layer_index)));
	    std::error_code ec;
	    filesystem::create_directories(sample_path.parent_path(), ec);
	    writeMonoSample(sample_path.string(), *content.getBuffer(), content.getNativeSampleRate());

	    auto sample_element = doc.NewElement("sample");
	    // Relative to the song's own directory (sample_path.parent_path()'s
	    // own last component, ".samples", joined with the file's own
	    // name) - never an absolute path, matching how the loader resolves
	    // it back the same way.
	    auto relative_path = sample_path.parent_path().filename() / sample_path.filename();
	    sample_element->SetAttribute("file", relative_path.string().c_str());
	    XMLParameterSource sample_parameters(sample_element);
	    content.storeParameters(sample_parameters);
	    clip_element->InsertEndChild(sample_element);
	  }
	} else {
	  auto & pattern = clip.getLeafPattern();
	  auto pattern_element = doc.NewElement("pattern");
	  storePatternContent(doc, pattern_element, pattern, track_tuning);
	  clip_element->InsertEndChild(pattern_element);
	}
	track_clips_element->InsertEndChild(clip_element);
      }
    }
  }

  // Sweeps <song-stem>.samples/ for any .wav that no longer corresponds
  // to a live sample clip or background bed anywhere in the song -
  // deleteClip()'s own "purely in-memory" contract (nothing on disk
  // changes as a side effect of an edit) means a deleted clip's own
  // sidecar file, or a background bed cleared/never re-merged into, is
  // only ever cleaned up here, at the one moment the artist actually
  // asked to persist the current state, not the moment the edit
  // happened. Independent of has_clips above - even a song with no
  // sample clips left at all can still have a leftover .samples/
  // directory from before. A missing directory (nothing was ever
  // recorded/loaded) isn't an error, just nothing to sweep.
  {
    unordered_set<string> live_ids;
    for (auto & [ track_id, clips ] : compiled->clips_by_track) {
      for (auto & clip : clips) {
	// One stem per real layer, not just clip.getId() alone - a
	// multi-layer clip's later takes live under their own suffixed
	// stem (sampleSidecarPath()'s own comment), which would otherwise
	// read as orphaned and get swept the very next save.
	auto & layers = clip.getSampleLayers();
	for (size_t layer_index = 0; layer_index < layers.size(); layer_index++) {
	  if (layers[layer_index].getBuffer()) {
	    live_ids.insert(filesystem::path(sampleSidecarPath(filename, clip.getId(), static_cast<int>(layer_index))).stem().string());
	  }
	}
      }
    }
    for (auto & [ track_id, background ] : compiled->arrangement.getSampleBackgroundsByTrack()) {
      if (background.getBuffer()) live_ids.insert(sampleBackgroundStem(track_id));
    }

    filesystem::path song_path(filename);
    auto samples_dir = song_path.parent_path() / (song_path.stem().string() + ".samples");
    error_code ec;
    if (filesystem::is_directory(samples_dir, ec)) {
      for (auto & entry : filesystem::directory_iterator(samples_dir, ec)) {
	if (entry.path().extension() != ".wav") continue;
	if (live_ids.count(entry.path().stem().string())) continue;
	filesystem::remove(entry.path(), ec);
      }
    }
  }

  auto locator_map = getLocators();
  if (!locator_map.empty()) {
    auto locators = doc.NewElement("locators");
    root->InsertEndChild(locators);
    for (auto & [ row, name ] : locator_map) {
      auto locator = doc.NewElement("locator");
      locator->SetAttribute("row", row);
      locator->SetText(name.c_str());
      locators->InsertEndChild(locator);
    }
  }

  // One <scene> per scene position, in order; trailing empty ones aren't written.
  auto named_scenes = sceneCount();
  while (named_scenes > 0 && getSceneName(named_scenes - 1).empty() && getSceneTempo(named_scenes - 1) == 0 && !getSceneTimeSignature(named_scenes - 1).isSet()) named_scenes--;
  if (named_scenes > 0) {
    auto scenes = doc.NewElement("scenes");
    root->InsertEndChild(scenes);
    for (int i = 0; i < named_scenes; i++) {
      auto scene = doc.NewElement("scene");
      if (!getSceneName(i).empty()) scene->SetAttribute("name", getSceneName(i).c_str());
      if (getSceneTempo(i) > 0) scene->SetAttribute("tempo", getSceneTempo(i));
      if (getSceneTimeSignature(i).isSet()) scene->SetAttribute("timeSignature", getSceneTimeSignature(i).toString().c_str());
      scenes->InsertEndChild(scene);
    }
  }

  // <arrangement> - the one timeline. Each track's own content is grouped
  // under one element per kind ("track" once, not on every child).
  auto arrangement_element = doc.NewElement("arrangement");
  root->InsertEndChild(arrangement_element);
  auto & timeline = compiled->arrangement;

  for (auto & [ track_id, pattern ] : timeline.getPatternsByTrack()) {
    auto track = getMasterTrack().getChildByInternalId(track_id);
    assert(track);
    if (!track) continue;

    auto pattern_element = doc.NewElement("pattern");
    pattern_element->SetAttribute("track", trackReferenceText(*this, track_id).c_str());
    storePatternContent(doc, pattern_element, pattern, getTuningForTrack(*track));
    arrangement_element->InsertEndChild(pattern_element);
  }

  // A placed clip is its id as the <instance>'s text, "OFF" an explicit
  // stop - like <note>/<command>, not an attribute.
  for (auto & [ track_id, track_instances ] : timeline.getInstancesByTrack()) {
    if (track_instances.empty()) continue;
    auto track = getMasterTrack().getChildByInternalId(track_id);
    assert(track);
    if (!track) continue;

    auto instances_element = doc.NewElement("instances");
    instances_element->SetAttribute("track", trackReferenceText(*this, track_id).c_str());
    for (auto & [ row, clip_id ] : track_instances) {
      auto instance_element = doc.NewElement("instance");
      instance_element->SetAttribute("row", static_cast<int>(row));
      instance_element->SetText(clip_id.c_str());
      instances_element->InsertEndChild(instance_element);
    }
    arrangement_element->InsertEndChild(instances_element);
  }

  for (auto & [ track_id, background ] : timeline.getSampleBackgroundsByTrack()) {
    if (!background.getBuffer()) continue;
    auto track = getMasterTrack().getChildByInternalId(track_id);
    assert(track);
    if (!track) continue;

    filesystem::path sample_path(sampleBackgroundSidecarPath(filename, track_id));
    std::error_code ec;
    filesystem::create_directories(sample_path.parent_path(), ec);
    writeMonoSample(sample_path.string(), *background.getBuffer(), background.getNativeSampleRate());

    auto background_element = doc.NewElement("sampleBackground");
    background_element->SetAttribute("track", trackReferenceText(*this, track_id).c_str());
    auto relative_path = sample_path.parent_path().filename() / sample_path.filename();
    background_element->SetAttribute("file", relative_path.string().c_str());
    arrangement_element->InsertEndChild(background_element);
  }

  for (auto & track : getMasterTrack().getChildren()) {
    storeChildTrack(*track, doc, tracks);
  }

  for (auto & instrument : getInstrumentPool().getInstruments()) {
    storeChildTrack(*instrument, doc, instruments);
  }
  
  doc.SaveFile(filename.c_str());

  setlocale(LC_ALL, oldLocale.c_str());
}
  
void
Song::loadParameters(const ParameterSource & input) {
  SongObject::loadParameters(input);

  auto song_tuning = parse_tuning(input.get<std::string>("temperament"), Tuning::EDO31);
  setTuning(song_tuning);

  auto key_text = input.get<std::string>("key");
  if (!key_text.empty()) setKey(Note::stringToKey(song_tuning, key_text));

  setScale(scaleFromString(input.get<std::string>("scale")));

  setTempo(input.get<int>("tempo", 90));
  setTimeSignature({4, 4});
  auto signature = TimeSignature::parse(input.get<std::string>("timeSignature"));
  if (signature && signature->isSet()) setTimeSignature(*signature);
  auto running = TimeSignature::parse(input.get<std::string>("transportTimeSignature"));
  if (running && running->isSet())
    setRunningBars({*running, input.get<int>("transportBarOrigin", 0)});
  else
    clearRunningBars();
  setSwing(input.get<int>("swing", swing::kStraight));
  setRecordQuantize(input.get<bool>("recordQuantize", false));

  setEarHeight(input.get<float>("earHeight", constants::DEFAULT_EAR_HEIGHT));
  setFloorReflectionEnabled(input.get<bool>("floorReflection", constants::DEFAULT_FLOOR_REFLECTION_ENABLED));
  setFloorReflectionStrength(input.get<float>("floorReflectionStrength", constants::DEFAULT_FLOOR_REFLECTION_STRENGTH));
  setGroundAbsorption(input.get<float>("groundAbsorption", constants::DEFAULT_GROUND_ABSORPTION));

  // The bus (reverb/delay/...) is not a <song> attribute - it's the
  // <bus> child element, parsed separately in Song::open() (mirroring
  // how <tracks>/<instruments>/<arrangement> are handled there too, not
  // here). resetBusToDefaults() puts both slots back at their compiled
  // defaults first, so a Song object reused for a second open() call
  // doesn't retain a stale bus configuration from whatever it loaded
  // previously.
  resetBusToDefaults();
}

const std::string &
Song::getLocator(int row) const {
  static const std::string none;
  auto index = locatorIndex(row);
  auto * children = doc_->get(doc_->root())->children(songschema::kLocatorsSlot);
  if (!children || index >= children->size()) return none;
  auto node = (*children)[index];
  return doc::get(*doc_, node, songschema::kLocatorRow) == row ? doc::getRef(*doc_, node, songschema::kLocatorText) : none;
}

void
Song::setLocator(int row, std::string name) {
  Edit edit(*this, "set locator");
  auto index = locatorIndex(row);
  auto * children = doc_->get(doc_->root())->children(songschema::kLocatorsSlot);
  doc::NodeId existing = doc::kNoNode;
  if (children && index < children->size() && doc::get(*doc_, (*children)[index], songschema::kLocatorRow) == row) existing = (*children)[index];
  if (name.empty()) {
    if (existing != doc::kNoNode) doc_->removeChild(doc_->root(), songschema::kLocatorsSlot, index);
    else edit.discard();
  } else if (existing != doc::kNoNode) {
    doc::set(*doc_, existing, songschema::kLocatorText, name);
  } else {
    auto node = doc_->create("locator", { { songschema::kLocatorRow.key, doc::toValue(row) }, { songschema::kLocatorText.key, doc::toValue(name) } });
    doc_->insertChild(doc_->root(), songschema::kLocatorsSlot, index, node);
  }
}

std::map<int, std::string>
Song::getLocators() const {
  std::map<int, std::string> out;
  if (auto * children = doc_->get(doc_->root())->children(songschema::kLocatorsSlot)) {
    for (auto node : *children) out[doc::get(*doc_, node, songschema::kLocatorRow)] = doc::get(*doc_, node, songschema::kLocatorText);
  }
  return out;
}

size_t
Song::locatorIndex(int row) const {
  auto * children = doc_->get(doc_->root())->children(songschema::kLocatorsSlot);
  if (!children) return 0;
  size_t lo = 0, hi = children->size();
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    if (doc::get(*doc_, (*children)[mid], songschema::kLocatorRow) < row) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

int
Song::sceneCount() const {
  auto * children = doc_->get(doc_->root())->children(songschema::kScenesSlot);
  return children ? static_cast<int>(children->size()) : 0;
}

void
Song::clearScenes() {
  Edit edit(*this, "clear scenes");
  while (sceneCount() > 0) doc_->removeChild(doc_->root(), songschema::kScenesSlot, static_cast<size_t>(sceneCount() - 1));
}

doc::NodeId
Song::sceneNode(int scene) const {
  auto * children = doc_->get(doc_->root())->children(songschema::kScenesSlot);
  return scene >= 0 && children && static_cast<size_t>(scene) < children->size() ? (*children)[static_cast<size_t>(scene)] : doc::kNoNode;
}

doc::NodeId
Song::ensureSceneNode(int scene) {
  if (scene < 0) return doc::kNoNode;
  while (sceneCount() <= scene) {
    auto node = doc_->create("scene");
    doc_->insertChild(doc_->root(), songschema::kScenesSlot, static_cast<size_t>(sceneCount()), node);
  }
  return sceneNode(scene);
}

const std::string &
Song::getSceneName(int scene) const {
  return doc::getRef(*doc_, sceneNode(scene), songschema::kSceneName);
}

int
Song::getSceneTempo(int scene) const {
  return doc::get(*doc_, sceneNode(scene), songschema::kSceneTempo);
}

TimeSignature
Song::getSceneTimeSignature(int scene) const {
  auto node = sceneNode(scene);
  return { doc::get(*doc_, node, songschema::kSceneTimeNumerator), doc::get(*doc_, node, songschema::kSceneTimeDenominator) };
}

void
Song::setSceneName(int scene, std::string name) {
  if (scene < 0) return;
  Edit edit(*this, "set scene name");
  doc::set(*doc_, ensureSceneNode(scene), songschema::kSceneName, std::move(name));
}

void
Song::setSceneTempo(int scene, int bpm) {
  if (scene < 0) return;
  Edit edit(*this, "set scene tempo");
  doc::set(*doc_, ensureSceneNode(scene), songschema::kSceneTempo, std::max(bpm, 0));
}

void
Song::setSceneTimeSignature(int scene, TimeSignature signature) {
  if (scene < 0) return;
  Edit edit(*this, "set scene time signature");
  bool valid = signature.isSet() && TimeSignature::validDenominator(signature.denominator);
  auto node = ensureSceneNode(scene);
  doc::set(*doc_, node, songschema::kSceneTimeNumerator, valid ? signature.numerator : 0);
  doc::set(*doc_, node, songschema::kSceneTimeDenominator, valid ? signature.denominator : 0);
}

void
Song::setTimeSignature(TimeSignature signature) {
  if (!signature.isSet() || !TimeSignature::validDenominator(signature.denominator)) return;
  Edit edit(*this, "set time signature");
  doc::set(*doc_, doc_->root(), songschema::kTimeNumerator, signature.numerator);
  doc::set(*doc_, doc_->root(), songschema::kTimeDenominator, signature.denominator);
}

RunningBars
Song::getRunningBars() const {
  return { { read(songschema::kTransportNumerator), read(songschema::kTransportDenominator) }, read(songschema::kTransportOrigin) };
}

void
Song::setRunningBars(RunningBars running) {
  // The audio thread owns the running signature; this is its mirror.
  Edit edit(*this, "mirror running bars", Edit::Kind::STRUCTURE, Edit::Origin::SYNC);
  doc::set(*doc_, doc_->root(), songschema::kTransportNumerator, running.signature.numerator);
  doc::set(*doc_, doc_->root(), songschema::kTransportDenominator, running.signature.denominator);
  doc::set(*doc_, doc_->root(), songschema::kTransportOrigin, running.origin);
}

int
Song::getArrangementLength() const {
  int end = 0;
  auto arrangement = getArrangement();
  for (auto & [ track_id, pattern ] : arrangement.getPatternsByTrack()) end = std::max(end, pattern.getContentEnd());
  for (auto & [ track_id, instances ] : arrangement.getInstancesByTrack()) {
    auto clips = getClips(track_id);
    for (auto & [ row, clip_id ] : instances) {
      int length = 0; // a stop ends content, nothing plays on its row
      if (clip_id != "OFF") length = 1;
      for (auto clip : clips) {
        if (clip.getId() == clip_id) { length = std::max(clip.getLength(), 1); break; }
      }
      end = std::max(end, static_cast<int>(row) + length);
    }
  }
  for (auto & [ track_id, background ] : arrangement.getSampleBackgroundsByTrack()) end = std::max(end, background.getRowCount(getTempo()));
  if (auto last = getLocators(); !last.empty()) end = std::max(end, last.rbegin()->first + 1);
  return getArrangementBars().roundUpToBar(end);
}

std::string
Song::formatPosition(int absolute_row) const {
  auto row = std::max(absolute_row, 0);
  auto bars = getBarsAt(row);
  // Numbering carries on from the bar the running bars began in.
  auto bar = bars.barIndex(row);
  auto running = getRunningBars();
  if (running.isActive() && row >= running.origin) bar += getArrangementBars().barIndex(running.origin);
  auto in_bar = bars.rowInBar(row);
  return std::to_string(bar + 1) + "." + std::to_string(in_bar / bars.beatRows() + 1) + "." + std::to_string(in_bar % bars.beatRows() + 1);
}

void
Song::storeParameters(ParameterSource & output) const {
  SongObject::storeParameters(output);

  if (getKey() >= 0) output.set("key", Note::keyToString(getTuning(), getKey()));
  if (getScale() != Scale::NONE) output.set("scale", to_string(getScale()));
  output.set("temperament", to_string(getTuning()));
  output.set("tempo", getTempo());
  if (getTimeSignature() != TimeSignature{4, 4}) output.set("timeSignature", getTimeSignature().toString());
  if (getRunningBars().isActive()) {
    output.set("transportTimeSignature", getRunningBars().signature.toString());
    output.set("transportBarOrigin", getRunningBars().origin);
  }
  output.set("swing", getSwing(), swing::kStraight);
  if (getRecordQuantize()) output.set("recordQuantize", true);

  output.set("earHeight", getEarHeight(), constants::DEFAULT_EAR_HEIGHT);
  if (getFloorReflectionEnabled() != constants::DEFAULT_FLOOR_REFLECTION_ENABLED) output.set("floorReflection", getFloorReflectionEnabled());
  output.set("floorReflectionStrength", getFloorReflectionStrength(), constants::DEFAULT_FLOOR_REFLECTION_STRENGTH);
  output.set("groundAbsorption", getGroundAbsorption(), constants::DEFAULT_GROUND_ABSORPTION);
}

vector<int>
Song::getRootTrackIds() const {
  return SongStructure(*this).getOrderedTrackIds();
}

vector<int>
Song::getPlayableTrackIds() const {
  SongStructure structure(*this);
  vector<int> ids;
  for (auto id : getRootTrackIds()) {
    if (structure.getBaselineInfo(id).color_ordinal_ >= 0) ids.push_back(id);
  }
  return ids;
}

vector<int>
Song::getScaleDegreesWindow(int start_index, int count, bool major_if_none) const {
  auto tuning = getTuning();
  auto key_note_number = getKey();
  auto scale = getScale();
  auto edo_steps = edoStepsFor(tuning);
  if (edo_steps <= 0 || count <= 0) return {}; // no interval structure (Tuning::PERCUSSION) to have degrees of at all

  // Same tonic-pitch-class extraction as LaunchpadManager::resolveNote()'s
  // own comment: getKey() is a full note value with its own baked-in
  // octave (Note::stringToKey()'s own octave-4 default whenever the key
  // text omits one) - only its pitch class matters here.
  auto tonic = key_note_number >= 0 ? ((key_note_number % edo_steps) + edo_steps) % edo_steps : 0;

  auto degree_names = scaleDegreeNames(scale == Scale::NONE && major_if_none ? Scale::MAJOR : scale);
  vector<int> offsets_from_tonic;
  if (degree_names.empty()) {
    // No scale chosen (or Scale::NONE resolved nothing) - the plain
    // chromatic scale instead, one degree per semitone/edo-step, cycling
    // every edo_steps the same way a named scale's own degree list cycles
    // every full pass through it below.
    for (int i = 0; i < edo_steps; i++) offsets_from_tonic.push_back(i);
  } else {
    // Each degree name resolved as its own interval from C under this
    // song's actual tuning (Note::stringToKey(tuning_, name) - stringToKey(tuning_, "C")),
    // then added to the tonic below - see this method's own header
    // comment for why this is what keeps the same degree_names list
    // correct under every tuning rather than needing one hardcoded
    // interval set per one.
    auto c_value = Note::stringToKey(tuning, "C");
    for (auto & name : degree_names) offsets_from_tonic.push_back(Note::stringToKey(tuning, name) - c_value);
  }
  auto n = static_cast<int>(offsets_from_tonic.size());
  if (n <= 0) return {};

  // Deliberately never wrapped mod edo_steps: index i's own value keeps
  // climbing past the octave boundary rather than folding back below the
  // tonic (e.g. a transposed scale's own upper degrees genuinely landing
  // above the octave point), and any index outside the degree list's own
  // [0, n) range cycles back through it one octave (edo_steps) higher or
  // lower per full wrap - so a 7-note scale's own index 7 is always its
  // tonic repeated an octave up, index -1 its own 7th degree an octave
  // down, and so on indefinitely in both directions (this method's own
  // header comment covers the "current scale is octave-periodic"
  // assumption that relies on).
  vector<int> degrees;
  for (int i = 0; i < count; i++) {
    auto index = start_index + i;
    auto wraps = index >= 0 ? index / n : -((-index + n - 1) / n); // floor division, n > 0
    auto within = index - wraps * n;
    degrees.push_back(tonic + offsets_from_tonic[static_cast<size_t>(within)] + wraps * edo_steps);
  }
  return degrees;
}
