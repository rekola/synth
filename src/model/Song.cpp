#include "Song.h"

#include "../state/SongState.h"

#include "InstrumentTrack.h"
#include "PercussionTrack.h"
#include "DrumMachineTrack.h"
#include "Group.h"
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

// Parses a <pattern>'s own <note>/<command> children (and optional
// `length` attribute) directly into `pattern` - shared by the per-scene
// reader (Scene::patterns_by_track_id_'s own entry) and the pattern-pool
// reader below, which parse the identical <pattern> shape into two
// different kinds of owning container. false (with the malformed-command
// diagnostic already printed) on a corrupt <command>, matching both
// readers' own "bail the whole load out" contract on that.
static bool parsePatternContent(XMLElement & pattern_element, Pattern & pattern, Tuning tuning, const string & filename) {
  auto length_text = pattern_element.Attribute("length");
  if (length_text) pattern.setLength(atoi(length_text));

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
    auto data_text = it->Attribute("data");

    if (data_text) {
      int row = row_text ? atoi(row_text) : 0;
      // setData(), not the Command(string_view) constructor - see the
      // scene reader's own original comment on this: untrusted file data
      // has to be actually detected as malformed here, not silently
      // fall back to a defined-but-wrong "----".
      Command command;
      if (!command.setData(data_text)) {
	fmt::print(stderr, "Malformed command \"{}\" at row {} in {}\n", data_text, row, filename);
	return false;
      }
      pattern.setCommand(row, command);
    }
  }
  return true;
}

static Tuning parse_tuning(string_view tuning_text, Tuning default_tuning = Tuning::TET12) {
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
  if (name == "drumMachineTrack") return make_unique<DrumMachineTrack>();
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

// A <drumMachineTrack>'s own <lane> children describe its kit - which
// drums it can play, not what triggers when (that's an ordinary per-scene
// Pattern now, like any other track - DrumMachineTrack.h's own comment).
// `note` is the same GM-percussion mnemonic (Note::keyToString()/
// stringToKey(), Tuning::PERCUSSION) a <note> element's own value already
// uses, not a raw integer.
static void loadDrumMachineData(DrumMachineTrack & track, XMLElement & element) {
  bool had_any_lane = false;
  for (auto it = element.FirstChildElement("lane"); it; it = it->NextSiblingElement("lane")) {
    auto note_text = it->Attribute("note");
    if (!note_text) continue;
    track.addLane(Note::stringToKey(Tuning::PERCUSSION, note_text));
    had_any_lane = true;
  }
  // No <lane> child at all (a hand-edited/older file that never mentions
  // a kit) means the file never said anything about lanes one way or the
  // other - give it the same default rock kit the interactive
  // "add-drum-machine-track" command would (seedDefaultKit()'s own
  // comment), rather than leaving a silent, lane-less track behind. A
  // file that lists at least one <lane> is being explicit about its kit
  // (down to genuinely zero real drums, if every listed note failed to
  // resolve), so it's left alone here.
  if (!had_any_lane) track.seedDefaultKit();
}

static void storeDrumMachineData(const DrumMachineTrack & track, XMLDocument & doc, XMLElement * track_element) {
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

  auto drum_machine_track = dynamic_cast<DrumMachineTrack *>(track.get());
  if (drum_machine_track) {
    loadDrumMachineData(*drum_machine_track, element);
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
// per-scene writer and the pattern-pool writer below. Reads the raw
// row->note-columns map directly (sorted, since it's an unordered_map)
// rather than looping some external row bound: a pooled Pattern has no
// scene/song pattern-length context to bound one by, and a scene's own
// inline Pattern's raw storage never holds anything past its own
// effective length in the first place (every write already redirects
// there via getEffectiveRow() - see Pattern.h), so this finds the exact
// same rows a bounded loop up to the song's own pattern length would.
static void storePatternContent(XMLDocument & doc, XMLElement * pattern_element, const Pattern & pattern, Tuning tuning) {
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

  for (auto & [ row, command ] : pattern.getCommands()) {
    auto data = to_string(command);
    auto command_element = doc.NewElement("command");
    command_element->SetAttribute("row", static_cast<int>(row));
    command_element->SetAttribute("data", data.c_str());
    pattern_element->InsertEndChild(command_element);
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

  auto drum_machine_track = dynamic_cast<const DrumMachineTrack *>(&track);
  if (drum_machine_track) {
    storeDrumMachineData(*drum_machine_track, doc, track_element);
  }

  auto generic_instrument = dynamic_cast<const GenericInstrument *>(&track);
  if (generic_instrument) {
    storeGeneratorOverrides(*generic_instrument, doc, track_element);
  }

  target_element->InsertEndChild(track_element);
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
	if (instrument) {
	  addInstrument(move(instrument));
	}
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
	if (track) {
	  addTrack(move(track));
	}
      }
    }
    
    // Sibling of <tracks>/<scenes> - Song's own flat, per-track pattern
    // pool (Song.h's own getPooledPatterns() comment), read before
    // <scenes> since it needs nothing from there. Each <pattern> is the
    // exact same shape a scene's own inline one uses (parsePatternContent()
    // above), just addressed by (track, vector position) instead of a
    // scene.
    auto pattern_pool = song->FirstChildElement("patterns");
    if (pattern_pool) {
      for (auto it = pattern_pool->FirstChildElement("pattern"); it ; it = it->NextSiblingElement("pattern")) {
	auto track_text = it->Attribute("track");
	auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	if (!track) continue;

	Pattern pattern;
	auto name_text = it->Attribute("name");
	if (name_text) pattern.setName(name_text);

	if (!parsePatternContent(*it, pattern, getTuningForTrack(*track), filename)) {
	  setlocale(LC_ALL, oldLocale.c_str());
	  return false;
	}
	addPooledPattern(track->getInternalId(), std::move(pattern));
      }
    }

    auto scenes = song->FirstChildElement("scenes");
    if (scenes) {
      for (auto it = scenes->FirstChildElement("scene"); it ; it = it->NextSiblingElement("scene") ) {
	auto & scene = addScene(Scene());
	scene.loadParameters(XMLParameterSource(it));

	for (auto it2 = it->FirstChildElement("annotation"); it2; it2 = it2->NextSiblingElement("annotation")) {
	  auto row_text = it2->Attribute("row");
	  if (row_text) {
	    int row = atoi(row_text);
	    auto s = it2->GetText();
	    scene.setAnnotation(row, s ? s : "");
	  }
	}

	// One <pattern track="..."> per track that has anything at this
	// scene - <note>/<command> no longer carry their own "track"
	// attribute (see the class's own header comment): which track
	// they belong to is resolved once per <pattern>, not once per
	// child element.
	for (auto it2 = it->FirstChildElement("pattern"); it2 ; it2 = it2->NextSiblingElement("pattern")) {
	  auto track_text = it2->Attribute("track");
	  auto track = track_text ? resolveTrackReference(*this, track_text) : nullptr;
	  if (!track) continue;

	  auto track_id = track->getInternalId();
	  auto & pattern = scene.getPatternsByTrack()[track_id];
	  if (!parsePatternContent(*it2, pattern, getTuningForTrack(*track), filename)) {
	    setlocale(LC_ALL, oldLocale.c_str());
	    return false;
	  }
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

  // Sibling of <tracks>/<scenes> - Song's own flat, per-track pattern pool
  // (Song.h's own getPooledPatterns() comment). Omitted entirely (not
  // written as an empty <patterns/>) when there's nothing in it, same
  // "default/empty state stores nothing" rule storeBusConfig() already
  // follows - most songs never use this feature at all, and a spurious
  // empty element on every one of them would just be diff noise. Walked
  // via getRootTrackIds() (tree order), not pattern_pool_by_track_
  // directly, for the same deterministic-output reason storeChildTrack()
  // walks the tree itself rather than some other, unordered collection.
  bool has_pooled_patterns = std::any_of(pattern_pool_by_track_.begin(), pattern_pool_by_track_.end(),
    [](auto & entry) { return !entry.second.empty(); });
  if (has_pooled_patterns) {
    auto pattern_pool = doc.NewElement("patterns");
    root->InsertEndChild(pattern_pool);
    for (auto track_id : getRootTrackIds()) {
      auto & pool_patterns = getPooledPatterns(track_id);
      if (pool_patterns.empty()) continue;

      auto track = getMasterTrack().getChildByInternalId(track_id);
      assert(track);
      if (!track) continue;
      auto track_tuning = getTuningForTrack(*track);
      auto track_ref = trackReferenceText(*this, track_id);

      for (auto & pattern : pool_patterns) {
	auto pattern_element = doc.NewElement("pattern");
	pattern_element->SetAttribute("track", track_ref.c_str());
	if (!pattern.getName().empty()) pattern_element->SetAttribute("name", pattern.getName().c_str());
	if (pattern.getLength() > 0) pattern_element->SetAttribute("length", pattern.getLength());
	storePatternContent(doc, pattern_element, pattern, track_tuning);
	pattern_pool->InsertEndChild(pattern_element);
      }
    }
  }

  auto scenes = doc.NewElement("scenes");
  root->InsertEndChild(scenes);

  for (auto & scene : getScenes()) {
    auto scene_element = doc.NewElement("scene");
    XMLParameterSource scene_parameters(scene_element);
    scene.storeParameters(scene_parameters);

    for (int row = 0; row < getPatternLength(); row++) {
      auto & annotation = scene.getAnnotation(row);
      if (!annotation.empty()) {
	auto annotation_element = doc.NewElement("annotation");
	annotation_element->SetAttribute("row", row);
	annotation_element->SetText(annotation.c_str());
	scene_element->InsertEndChild(annotation_element);
      }
    }

    // One <pattern track="..."> per track that has anything in this scene -
    // "track" moves here from every <note>/<command> (see this class's own
    // header comment), so it's resolved once per track instead of once per
    // element.
    for (auto & [ track_id, pattern ] : scene.getPatternsByTrack()) {
      auto track = getMasterTrack().getChildByInternalId(track_id);
      assert(track);
      if (!track) continue;

      auto track_tuning = getTuningForTrack(*track);
      auto track_ref = trackReferenceText(*this, track_id);

      auto pattern_element = doc.NewElement("pattern");
      pattern_element->SetAttribute("track", track_ref.c_str());
      // 0 (unset - see Pattern.h's own comment) is today's exact
      // behavior (tracks this song's own pattern length, no repeat), so
      // it's simply omitted rather than written as an explicit 0.
      if (pattern.getLength() > 0) pattern_element->SetAttribute("length", pattern.getLength());
      storePatternContent(doc, pattern_element, pattern, track_tuning);
      scene_element->InsertEndChild(pattern_element);
    }

    scenes->InsertEndChild(scene_element);
  }

  for (auto & track : getMasterTrack().getChildren()) {
    storeChildTrack(*track, doc, tracks);
  }

  for (auto & instrument : getInstruments()) {
    storeChildTrack(*instrument, doc, instruments);
  }
  
  doc.SaveFile(filename.c_str());

  setlocale(LC_ALL, oldLocale.c_str());
}
  
void
Song::loadParameters(const ParameterSource & input) {
  SongObject::loadParameters(input);

  auto song_tuning = parse_tuning(input.get<std::string>("temperament"), Tuning::TET12);
  setTuning(song_tuning);

  auto key_text = input.get<std::string>("key");
  if (!key_text.empty()) setKey(Note::stringToKey(song_tuning, key_text));

  setTempo(input.get<int>("tempo", 90));
  setPatternLength(input.get<int>("patternRows", 64));

  setEarHeight(input.get<float>("earHeight", constants::DEFAULT_EAR_HEIGHT));
  setFloorReflectionEnabled(input.get<bool>("floorReflection", constants::DEFAULT_FLOOR_REFLECTION_ENABLED));
  setFloorReflectionStrength(input.get<float>("floorReflectionStrength", constants::DEFAULT_FLOOR_REFLECTION_STRENGTH));
  setGroundAbsorption(input.get<float>("groundAbsorption", constants::DEFAULT_GROUND_ABSORPTION));

  // The bus (reverb/delay/...) is not a <song> attribute - it's the
  // <bus> child element, parsed separately in Song::open() (mirroring
  // how <tracks>/<instruments>/<scenes> are handled there too, not
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
  output.set("patternRows", getPatternLength(), 64);

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
