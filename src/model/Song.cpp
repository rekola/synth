#include "Song.h"

#include "../state/SongState.h"

#include "InstrumentTrack.h"
#include "PercussionTrack.h"
#include "SampleTrack.h"
#include "SampleContent.h"
#include "Group.h"
#include "../audio/SampleFileLoader.h"
#include "../instruments/NoteMultiplier.h"
#include "../instruments/Arpeggiator.h"
#include "../instruments/Oscillator.h"
#include "../instruments/Noise.h"
#include "../instruments/LFO.h"
#include "../instruments/GenericInstrument.h"

#include "../effects/Distortion.h"
#include "../effects/ResonantFilter.h"
#include "../effects/BiquadFilter.h"
#include "../effects/Chorus.h"
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
static Track * resolveTrackReference(Song & song, const char * text) {
  auto track = song.getMasterTrack().getChildById(text);
  if (track) return track;
  return song.getMasterTrack().getChildByInternalId(atoi(text));
}

string
sampleSidecarPath(const string & song_filename, const string & clip_id) {
  filesystem::path song_path(song_filename);
  auto sample_path = song_path.parent_path() / (song_path.stem().string() + ".samples") / (clip_id + ".wav");
  return sample_path.string();
}

// A SampleTrack's own background bed's sidecar .wav stem/path - same
// `<song-stem>.samples/` sidecar directory a real clip's own sidecar
// already uses (sampleSidecarPath() above), just named by (section id,
// track id) instead of a clip's own stable id. Keyed by the section's own
// stable id (Song::generateUniqueSectionId(), assigned lazily the moment a
// section first gets a real background bed - ArrangementOps.cpp's own
// mergeClipToBackground()), deliberately not its ordinal position in
// Song::getSections(): inserting/reordering sections is a normal edit, and
// an ordinal position shifting under every section after the edit would
// rename (and orphan-then-recreate) every one of their own background
// sidecar files on the very next save for no real reason.
static string
sampleBackgroundStem(const string & section_id, int track_id) {
  return "background_" + section_id + "_" + to_string(track_id);
}

static string
sampleBackgroundSidecarPath(const string & song_filename, const string & section_id, int track_id) {
  filesystem::path song_path(song_filename);
  auto sample_path = song_path.parent_path() / (song_path.stem().string() + ".samples") / (sampleBackgroundStem(section_id, track_id) + ".wav");
  return sample_path.string();
}

// Parses a <pattern>'s own <note>/<command> children (and optional
// `length` attribute) directly into `pattern` - shared by the per-section
// reader (Section::patterns_by_track_id_'s own entry) and the clip reader
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
      // setData(), not the Command(string_view) constructor - see the
      // section reader's own original comment on this: untrusted file data
      // has to be actually detected as malformed here, not silently
      // fall back to a defined-but-wrong "----".
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

static Tuning parse_tuning(string_view tuning_text, Tuning default_tuning = Tuning::TET31) {
  if (tuning_text == "12edo") return Tuning::TET12;
  else if (tuning_text == "31edo") return Tuning::TET31;
  else if (tuning_text == "19edo") return Tuning::TET19;
  else if (tuning_text == "53edo") return Tuning::TET53;
  assert(0);
  return default_tuning;
}

static unique_ptr<Track> createTrack(string_view name) {  
  if (name == "track") return make_unique<InstrumentTrack>();
  if (name == "percussionTrack") return make_unique<PercussionTrack>();
  if (name == "sampleTrack") return make_unique<SampleTrack>();
  if (name == "arpeggiatorTrack") return make_unique<Arpeggiator>();
  else if (name == "group") return make_unique<Group>();

  // effects
  else if (name == "distortion") return make_unique<Distortion>();
  else if (name == "resonantFilter") return make_unique<ResonantFilter>();
  else if (name == "biquadFilter") return make_unique<BiquadFilter>();
  else if (name == "chorus") return make_unique<Chorus>();
  else if (name == "tremolo") return make_unique<Tremolo>();
  else if (name == "multiply") return make_unique<NoteMultiplier>();
  else if (name == "envelope") return make_unique<EnvelopeFilter>();
  else if (name == "amplifier") return make_unique<Amplifier>();
  else if (name == "compressor") return make_unique<Compressor>();
  else if (name == "tapeDegradation") return make_unique<TapeDegradation>();
  
  // instruments
  else if (name == "instrument") return make_unique<GenericInstrument>();
  else if (name == "oscillator") return make_unique<Oscillator>(WaveformType::SAW);
  else if (name == "noise") return make_unique<Noise>();
  else if (name == "LFO") return make_unique<LFO>();

  else {
    assert(0);
    return unique_ptr<Track>(nullptr);
  }
}

// A <percussionTrack>'s own <lane> children describe its kit - which
// drums it can play, not what triggers when (that's an ordinary per-section
// Pattern now, like any other track - PercussionTrack.h's own comment). No
// <lane> children at all is a plain, lane-less percussion track, not a
// special case to fill in - see PercussionTrack.h's own header comment on
// why zero lanes is an ordinary, meaningful state now. `note` is the same
// GM-percussion mnemonic (Note::keyToString()/stringToKey(),
// Tuning::PERCUSSION) a <note> element's own value already uses, not a raw
// integer.
static void loadPercussionLanes(PercussionTrack & track, XMLElement & element) {
  for (auto it = element.FirstChildElement("lane"); it; it = it->NextSiblingElement("lane")) {
    auto note_text = it->Attribute("note");
    if (!note_text) continue;
    track.addLane(Note::stringToKey(Tuning::PERCUSSION, note_text));
  }
}

static void storePercussionLanes(const PercussionTrack & track, XMLDocument & doc, XMLElement * track_element) {
  for (auto note : track.getLaneNotes()) {
    auto lane_element = doc.NewElement("lane");
    lane_element->SetAttribute("note", Note::keyToString(Tuning::PERCUSSION, note).c_str());
    track_element->InsertEndChild(lane_element);
  }
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
  auto track = createTrack(element.Name());
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

  auto percussion_track = dynamic_cast<PercussionTrack *>(track.get());
  if (percussion_track) {
    loadPercussionLanes(*percussion_track, element);
  }

  for (auto it = element.FirstChildElement(); it ; it = it->NextSiblingElement() ) {
    if (string_view(it->Name()) == "lane") continue; // data, not a nested track - handled above
    if (string_view(it->Name()) == "generator") continue; // data, not a nested track - handled above
    auto child = parseChildTrack(*it, provider);
    if (!child) return std::unique_ptr<Track>(nullptr);
    track->addChild(std::move(child));
  }

  return track;
}

// Writes <note>/<command> children into `pattern_element` for every row
// `pattern` actually has content on, in ascending row order - the write
// side of parsePatternContent() above, shared the same way by the
// per-section writer and the clip writer below. Reads the raw row->note-
// columns map directly (sorted, since it's an unordered_map) rather than
// looping some external row bound: a clip's own leaf Pattern has no
// section/song pattern-length context to bound one by, and a section's own
// inline Pattern's raw storage never holds anything past its own
// effective length in the first place (every write already redirects
// there via getEffectiveRow() - see Pattern.h), so this finds the exact
// same rows a bounded loop up to the song's own pattern length would.
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

  auto percussion_track = dynamic_cast<const PercussionTrack *>(&track);
  if (percussion_track) {
    storePercussionLanes(*percussion_track, doc, track_element);
  }

  auto generic_instrument = dynamic_cast<const GenericInstrument *>(&track);
  if (generic_instrument) {
    storeGeneratorOverrides(*generic_instrument, doc, track_element);
  }

  target_element->InsertEndChild(track_element);
}

// Walks the whole track tree fixing up every InstrumentTrack::
// instrument_id_ against a pool slot having just shifted - see Song.h's
// own doc comment on removeInstrument() for what "fixing up" means for a
// track pointing past vs. exactly at the removed slot. Recurses into
// every child regardless of type (Group/Effect wrappers included) - a
// pool index has no notion of "root track only" the way removeTrack()'s
// own id-based lookup does.
static void reindexInstrumentIds(Track & track, int removed_index) {
  auto * instrument_track = dynamic_cast<InstrumentTrack *>(&track);
  if (instrument_track) {
    auto id = instrument_track->getInstrumentId();
    if (id == removed_index) instrument_track->setInstrumentId(-1);
    else if (id > removed_index) instrument_track->setInstrumentId(id - 1);
  }
  for (auto & child : track.getChildren()) reindexInstrumentIds(*child, removed_index);
}

void
Song::removeInstrument(int index) {
  auto & instruments = instrument_pool_.getInstruments();
  if (index < 0 || index >= static_cast<int>(instruments.size())) return;
  instrument_pool_.removeInstrument(index);
  reindexInstrumentIds(*master_track_, index);
  incVersion();
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
  song.setBusSlotKind(slot, descriptor->kind);
  song.getBusSlot(slot).loadParameters(XMLParameterSource(&element));
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

Song::Song(Tuning tuning, short key)
  : tuning_(tuning), key_note_number_(key) {
  resetBusToDefaults();
  loadMasterTrackParameters(MemoryParameterSource());
}

void
Song::setBusSlotKind(int slot, BusEffectKind kind) {
  auto & descriptor = findBusEffectDescriptor(kind);
  auto effect = descriptor.factory(kPlaceholderBusSampleRate);
  if (slot == 0) { bus_slot_a_ = std::move(effect); bus_slot_a_kind_ = kind; }
  else { bus_slot_b_ = std::move(effect); bus_slot_b_kind_ = kind; }
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
      instrument_pool_.loadParameters(XMLParameterSource(instruments));
      for (auto it = instruments->FirstChildElement(); it; it = it->NextSiblingElement() ) {
	auto instrument = parseChildTrack(*it, provider);
	// Unrecognized (or, recursively, containing an unrecognized child)
	// is fatal to the whole load, not silently dropped - createTrack()'s
	// own assert(0) on an unknown element name is compiled out entirely
	// in a release build, so this is the only place that actually
	// catches it there.
	if (!instrument) {
	  fmt::print(stderr, "Unrecognized or malformed <{}> in {}\n", it->Name(), filename);
	  setlocale(LC_ALL, oldLocale.c_str());
	  return false;
	}
	addInstrument(move(instrument));
      }
    }
    // Resolves the pool's own default drum kit (see InstrumentPool::
    // prepare()'s own comment) - unconditional, not nested inside the
    // `if (instruments)` above, so a song with no <instruments> element at
    // all still gets one (defaults to "kit" - the same handling
    // loadParameters() never having run leaves default_kit_ in).
    instrument_pool_.prepare(provider);

    auto tracks = song->FirstChildElement("tracks");
    if (tracks) {
      loadMasterTrackParameters(XMLParameterSource(tracks));
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
    
    // Sibling of <tracks>/<sections> - Song's own flat, per-track clip list
    // (Song.h's own getClips() comment), read before <sections> since it
    // needs nothing from there. One <trackClips> per track that has any
    // clips at all, grouping that track's own <clip> children in order
    // (mirrors how the writer below already walks them, one track at a
    // time) rather than repeating a track reference on every single
    // <clip>. Each <clip> holds its own name/loop/length
    // (Clip::loadParameters()) plus a nested <pattern> for its leaf
    // track's own note/command content - the exact same shape a section's
    // own inline <pattern> uses (parsePatternContent() above), just with
    // no name/loop/length of its own (those are the enclosing <clip>'s).
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
	    auto & content = clip.getOrCreateSampleContent();
	    content.setBuffer(loaded.buffer);
	    content.setNativeSampleRate(loaded.rate);
	    content.loadParameters(XMLParameterSource(sample_element));
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

    // <sections>/<section> is the current tag pair (Section, formerly
    // Scene, before the "Scene" name got reserved for a Session-view
    // launch row instead - see CLAUDE.md's own GridMode comment); a file
    // saved before that rename still has <scenes>/<scene> instead, which
    // this falls back to reading as a read-only compatibility path -
    // Song::save() below always writes the current tag pair, so a file
    // only ever needs this fallback once, the first time it's resaved.
    //
    // firstNonEmpty() skips an empty <sections/> (or <scenes/>) rather
    // than just taking whichever comes first: several real songs in this
    // repo's own corpus (songtest14/17/18/19/19b, a.xml, scaletest_
    // 7limit_simple - confirmed already present at HEAD, so not something
    // this rename introduced) carry a leftover empty <sections/> stub
    // ahead of their real content - a dead artifact from an unrelated,
    // long-retired pre-Scene song format that used to spell its own
    // (different) top-level element the same way. The old <scenes> name
    // never collided with it, so this went unnoticed; <sections> does,
    // and FirstChildElement() alone would silently find the empty stub
    // and read the song as having no content at all.
    auto firstNonEmpty = [&song](const char * tag) -> XMLElement * {
      for (auto candidate = song->FirstChildElement(tag); candidate; candidate = candidate->NextSiblingElement(tag)) {
	if (candidate->FirstChildElement()) return candidate;
      }
      return nullptr;
    };
    auto sections = firstNonEmpty("sections");
    const char * section_tag = "section";
    if (!sections) {
      sections = firstNonEmpty("scenes");
      section_tag = "scene";
    }
    if (sections) {
      for (auto it = sections->FirstChildElement(section_tag); it ; it = it->NextSiblingElement(section_tag) ) {
	auto & section = addSection(Section());
	// A <section> with no "length" of its own (a pre-variable-length-
	// sections file included) just takes Section's own compiled default
	// (4 bars) - see Section::loadParameters()'s own comment.
	section.loadParameters(XMLParameterSource(it));

	for (auto it2 = it->FirstChildElement("annotation"); it2; it2 = it2->NextSiblingElement("annotation")) {
	  auto row_text = it2->Attribute("row");
	  if (row_text) {
	    int row = atoi(row_text);
	    auto s = it2->GetText();
	    section.setAnnotation(row, s ? s : "");
	  }
	}

	// One <pattern track="..."> per track that has anything at this
	// section - <note>/<command> no longer carry their own "track"
	// attribute (see the class's own header comment): which track
	// they belong to is resolved once per <pattern>, not once per
	// child element.
	for (auto it2 = it->FirstChildElement("pattern"); it2 ; it2 = it2->NextSiblingElement("pattern")) {
	  auto track_text = it2->Attribute("track");
	  auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	  if (!track) continue;

	  auto track_id = track->getInternalId();
	  auto & pattern = section.getPatternsByTrack()[track_id];
	  if (!parsePatternContent(*it2, pattern, getTuningForTrack(*track), filename)) {
	    setlocale(LC_ALL, oldLocale.c_str());
	    return false;
	  }
	}

	// One <arrangement track="..."> per track that has any instance
	// events, each holding that track's own <instance row="...">
	// children - see the writer's own comment (Song::save()) for the
	// shape.
	for (auto it2 = it->FirstChildElement("arrangement"); it2 ; it2 = it2->NextSiblingElement("arrangement")) {
	  auto track_text = it2->Attribute("track");
	  auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	  if (!track) continue;
	  auto track_id = track->getInternalId();

	  for (auto it3 = it2->FirstChildElement("instance"); it3 ; it3 = it3->NextSiblingElement("instance")) {
	    auto row_text = it3->Attribute("row");
	    if (!row_text) continue;
	    auto value_text = it3->GetText();
	    if (!value_text) continue;
	    section.setInstance(track_id, atoi(row_text), value_text);
	  }
	}

	// A SampleTrack's own background bed for this section (Section::
	// getOrCreateSampleBackgroundContent()) - one <sampleBackground
	// track="..." file="..."> per track that has one, the sample-content
	// sibling of a real clip's own <sample file="..."> above, just with
	// no in/out/originalTempo of its own (a background bed is never
	// trimmed or tempo-stretched - see Section.h's own comment).
	for (auto it2 = it->FirstChildElement("sampleBackground"); it2 ; it2 = it2->NextSiblingElement("sampleBackground")) {
	  auto track_text = it2->Attribute("track");
	  auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	  if (!track) continue;

	  auto file_attr = it2->Attribute("file");
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
	  auto & content = section.getOrCreateSampleBackgroundContent(track->getInternalId());
	  content.setBuffer(loaded.buffer);
	  content.setNativeSampleRate(loaded.rate);
	  // Self-healing for a hand-authored file that never gave this
	  // section its own "id" attribute - Song::generateUniqueSectionId()'s
	  // own comment on why a real background bed needs one.
	  if (section.getId().empty()) section.setId(generateUniqueSectionId());
	}
      }
    }
  }

  // Set the old locale before exiting
  setlocale(LC_ALL, oldLocale.c_str());
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
  instrument_pool_.storeParameters(instruments_parameters);
  root->InsertEndChild(instruments);

  auto tracks = doc.NewElement("tracks");
  XMLParameterSource tracks_parameters(tracks);
  getMasterTrack().storeParameters(tracks_parameters);
  root->InsertEndChild(tracks);

  // Sibling of <tracks>/<sections> - Song's own flat, per-track clip list
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
  bool has_clips = std::any_of(clips_by_track_.begin(), clips_by_track_.end(),
    [](auto & entry) { return !entry.second.empty(); });
  if (has_clips) {
    auto clips_element = doc.NewElement("clips");
    root->InsertEndChild(clips_element);
    for (auto track_id : getRootTrackIds()) {
      auto & clips = getClips(track_id);
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

	auto * content = clip.getSampleContent();
	if (content && content->getBuffer()) {
	  filesystem::path sample_path(sampleSidecarPath(filename, clip.getId()));
	  std::error_code ec;
	  filesystem::create_directories(sample_path.parent_path(), ec);
	  writeMonoSample(sample_path.string(), *content->getBuffer(), content->getNativeSampleRate());

	  auto sample_element = doc.NewElement("sample");
	  // Relative to the song's own directory (sample_path.parent_path()'s
	  // own last component, ".samples", joined with the file's own
	  // name) - never an absolute path, matching how the loader resolves
	  // it back the same way.
	  auto relative_path = sample_path.parent_path().filename() / sample_path.filename();
	  sample_element->SetAttribute("file", relative_path.string().c_str());
	  XMLParameterSource sample_parameters(sample_element);
	  content->storeParameters(sample_parameters);
	  clip_element->InsertEndChild(sample_element);
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
    for (auto & [ track_id, clips ] : clips_by_track_) {
      for (auto & clip : clips) {
	if (clip.getSampleContent() && clip.getSampleContent()->getBuffer()) live_ids.insert(clip.getId());
      }
    }
    for (auto & section : getSections()) {
      // Both real creation paths (mergeClipToBackground(), this file's
      // own <sampleBackground> reader above) already assign a section id
      // the moment a background bed is actually created - an empty id
      // here would mean a caller reached getOrCreateSampleBackgroundContent()
      // some other way, which isn't a case that exists today.
      if (section.getId().empty()) continue;
      for (auto & [ track_id, background ] : section.getSampleBackgroundsByTrack()) {
	if (background.getBuffer()) live_ids.insert(sampleBackgroundStem(section.getId(), track_id));
      }
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

  // <sections>/<section> - see the reader's own comment above on the
  // rename from <scenes>/<scene> (still read, never written).
  auto sections = doc.NewElement("sections");
  root->InsertEndChild(sections);

  for (auto & section : getSections()) {
    auto section_element = doc.NewElement("section");
    XMLParameterSource section_parameters(section_element);
    section.storeParameters(section_parameters);

    // Every annotation this section actually has, not just the ones within
    // its own current length - a row past the section's own bounds (e.g.
    // one left behind by a later shrink) is still real authored content,
    // and silently dropping it on save would be data loss.
    for (auto & [ row, annotation ] : section.getAnnotations()) {
      auto annotation_element = doc.NewElement("annotation");
      annotation_element->SetAttribute("row", static_cast<int>(row));
      annotation_element->SetText(annotation.c_str());
      section_element->InsertEndChild(annotation_element);
    }

    // One <pattern track="..."> per track that has anything in this
    // section - "track" moves here from every <note>/<command> (see this
    // class's own header comment), so it's resolved once per track
    // instead of once per element.
    for (auto & [ track_id, pattern ] : section.getPatternsByTrack()) {
      auto track = getMasterTrack().getChildByInternalId(track_id);
      assert(track);
      if (!track) continue;

      auto track_tuning = getTuningForTrack(*track);
      auto track_ref = trackReferenceText(*this, track_id);

      auto pattern_element = doc.NewElement("pattern");
      pattern_element->SetAttribute("track", track_ref.c_str());
      storePatternContent(doc, pattern_element, pattern, track_tuning);
      section_element->InsertEndChild(pattern_element);
    }

    // One <arrangement track="..."> per track that has any instance
    // events in this section, grouping them the same way <clips>'s own
    // <trackClips> groups a track's own clips - avoids repeating "track"
    // on every single <instance>. The value (a clip's own id, or "OFF"
    // for an explicit stop) is the element's own text content, matching
    // <note>/<command>, not an attribute.
    for (auto & [ track_id, track_instances ] : section.getInstancesByTrack()) {
      if (track_instances.empty()) continue;
      auto track = getMasterTrack().getChildByInternalId(track_id);
      assert(track);
      if (!track) continue;

      auto arrangement_element = doc.NewElement("arrangement");
      arrangement_element->SetAttribute("track", trackReferenceText(*this, track_id).c_str());
      for (auto & [ row, clip_id ] : track_instances) {
	auto instance_element = doc.NewElement("instance");
	instance_element->SetAttribute("row", static_cast<int>(row));
	instance_element->SetText(clip_id.c_str());
	arrangement_element->InsertEndChild(instance_element);
      }
      section_element->InsertEndChild(arrangement_element);
    }

    // One <sampleBackground track="..." file="..."> per track that has a
    // real background bed in this section (Section::
    // getSampleBackgroundsByTrack()) - the sample-content sibling of a
    // real clip's own <sample> above, just with no in/out/originalTempo
    // of its own (see Section.h's own comment on why). Written to the
    // same `<song-stem>.samples/` sidecar directory a real clip's own
    // audio already uses, just named by (section id, track id) instead of
    // a clip's own stable id - sampleBackgroundSidecarPath()'s own comment
    // has the full reasoning. Skipped (like the orphan sweep below) if
    // this section somehow has no id of its own - shouldn't happen, both
    // real creation paths already assign one.
    if (!section.getId().empty()) {
      for (auto & [ track_id, background ] : section.getSampleBackgroundsByTrack()) {
	if (!background.getBuffer()) continue;
	auto track = getMasterTrack().getChildByInternalId(track_id);
	assert(track);
	if (!track) continue;

	filesystem::path sample_path(sampleBackgroundSidecarPath(filename, section.getId(), track_id));
	std::error_code ec;
	filesystem::create_directories(sample_path.parent_path(), ec);
	writeMonoSample(sample_path.string(), *background.getBuffer(), background.getNativeSampleRate());

	auto background_element = doc.NewElement("sampleBackground");
	background_element->SetAttribute("track", trackReferenceText(*this, track_id).c_str());
	auto relative_path = sample_path.parent_path().filename() / sample_path.filename();
	background_element->SetAttribute("file", relative_path.string().c_str());
	section_element->InsertEndChild(background_element);
      }
    }

    sections->InsertEndChild(section_element);
  }

  for (auto & track : getMasterTrack().getChildren()) {
    storeChildTrack(*track, doc, tracks);
  }

  for (auto & instrument : instrument_pool_.getInstruments()) {
    storeChildTrack(*instrument, doc, instruments);
  }
  
  doc.SaveFile(filename.c_str());

  setlocale(LC_ALL, oldLocale.c_str());
}
  
void
Song::loadParameters(const ParameterSource & input) {
  SongObject::loadParameters(input);

  auto song_tuning = parse_tuning(input.get<std::string>("temperament"), Tuning::TET31);
  setTuning(song_tuning);

  auto key_text = input.get<std::string>("key");
  if (!key_text.empty()) setKey(Note::stringToKey(song_tuning, key_text));

  setTempo(input.get<int>("tempo", 90));
  setRowsPerBar(input.get<int>("rowsPerBar", 16));

  setEarHeight(input.get<float>("earHeight", constants::DEFAULT_EAR_HEIGHT));
  setFloorReflectionEnabled(input.get<bool>("floorReflection", constants::DEFAULT_FLOOR_REFLECTION_ENABLED));
  setFloorReflectionStrength(input.get<float>("floorReflectionStrength", constants::DEFAULT_FLOOR_REFLECTION_STRENGTH));
  setGroundAbsorption(input.get<float>("groundAbsorption", constants::DEFAULT_GROUND_ABSORPTION));

  // The bus (reverb/delay/...) is not a <song> attribute - it's the
  // <bus> child element, parsed separately in Song::open() (mirroring
  // how <tracks>/<instruments>/<sections> are handled there too, not
  // here). resetBusToDefaults() puts both slots back at their compiled
  // defaults first, so a Song object reused for a second open() call
  // doesn't retain a stale bus configuration from whatever it loaded
  // previously.
  resetBusToDefaults();
}

void
Song::storeParameters(ParameterSource & output) const {
  SongObject::storeParameters(output);

  if (getKey() >= 0) output.set("key", Note::keyToString(getTuning(), getKey()));
  output.set("temperament", to_string(getTuning()));
  output.set("tempo", getTempo());
  output.set("rowsPerBar", getRowsPerBar(), 16);

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
