#include "Song.h"

#include "SongState.h"

#include "InstrumentTrack.h"
#include "PercussionTrack.h"
#include "DrumMachineTrack.h"
#include "Group.h"
#include "NoteMultiplier.h"
#include "Arpeggiator.h"
#include "Oscilator.h"
#include "Noise.h"
#include "LFO.h"
#include "GenericInstrument.h"

#include "effects/Distortion.h"
#include "effects/ResonantFilter.h"
#include "effects/BiquadFilter.h"
#include "effects/Chorus.h"
#include "effects/Tremolo.h"
#include "effects/Amplifier.h"
#include "effects/EnvelopeFilter.h"
#include "effects/Compressor.h"

#include "bus/BusEffectRegistry.h"
#include "MemoryParameterSource.h"

#include "third_party/tinyxml2/tinyxml2.h"

#include "constants.h"

using namespace std;
using namespace tinyxml2;

class XMLParameterSource : public ParameterSource {
public:
  XMLParameterSource(XMLElement * element) : element_(element) { }
  XMLParameterSource(XMLElement * element, std::shared_ptr<std::unordered_map<std::string, int>> id_mapping)
    : ParameterSource(std::move(id_mapping)), element_(element) { }

  bool has(const std::string & name) const override { return element_->Attribute(name.c_str()) != 0; }

  void set(const std::string & name, int value) override { element_->SetAttribute(name.c_str(), value); }
  virtual void set(const std::string & name, float value) { element_->SetAttribute(name.c_str(), value); }
  virtual void set(const std::string & name, const std::string & value) { element_->SetAttribute(name.c_str(), value.c_str()); }
  
  int getInt(const std::string & name, int default_value = 0) const override {
    auto value = element_->Attribute(name.c_str());
    return value ? atoi(value) : default_value;
  }
  string getText(const std::string & name, const std::string & default_value) const override{
    auto value = element_->Attribute(name.c_str());
    return value ? value : default_value;
  }
  float getFloat(const std::string & name, float default_value = 0) const override {
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
  auto track = song.getTrackByInternalId(track_id);
  if (track && !track->getId().empty()) return track->getId();
  return to_string(track_id);
}

// The inverse of trackReferenceText() above: a track attribute may be
// either a track's own textual id or its raw internal id written as a
// decimal string (a track with no textual id of its own) - tried in that
// order, so a numeric-looking textual id (however unlikely) still wins
// over misreading it as an internal id. nullptr if neither resolves.
static Track * resolveTrackReference(Song & song, const char * text) {
  auto track = song.getTrackById(text);
  if (track) return track;
  return song.getTrackByInternalId(atoi(text));
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
  else if (name == "group") return make_unique<Group>();

  // effects
  else if (name == "distortion") return make_unique<Distortion>();
  else if (name == "resonantFilter") return make_unique<ResonantFilter>();
  else if (name == "biquadFilter") return make_unique<BiquadFilter>();
  else if (name == "chorus") return make_unique<Chorus>();
  else if (name == "tremolo") return make_unique<Tremolo>();
  else if (name == "multiply") return make_unique<NoteMultiplier>();
  else if (name == "arpeggiator") return make_unique<Arpeggiator>();
  else if (name == "envelope") return make_unique<EnvelopeFilter>();
  else if (name == "amplifier") return make_unique<Amplifier>();
  else if (name == "compressor") return make_unique<Compressor>();
  
  // instruments
  else if (name == "genericInstrument") return make_unique<GenericInstrument>();
  else if (name == "oscilator") return make_unique<Oscilator>(WaveformType::SAW);
  else if (name == "noise") return make_unique<Noise>();
  else if (name == "LFO") return make_unique<LFO>();

  else {
    assert(0);
    return unique_ptr<Track>(nullptr);
  }
}

// A <drumMachine> element carries a DrumMachineTrack's own step-sequencer
// data (lanes + steps), never Pattern row data (see DrumMachineTrack.h's
// own comment) - it is a data blob nested under a <drumMachineTrack>
// element, not a nested Track, so it's parsed/written here directly
// rather than through parseChildTrack()/storeChildTrack()'s generic
// per-child-track recursion (which would otherwise try to createTrack()
// it and fail). Mirrors the hand-written <bus>/<scenes> shape elsewhere
// in this file rather than reusing that generic recursion, for the same
// reason those aren't generic either: the child data isn't itself a Track.
static void loadDrumMachineData(DrumMachineTrack & track, XMLElement * drum_machine_element) {
  // No <drumMachine> element at all (a hand-edited/older file that never
  // mentions a sequence) means the file never said anything about lanes
  // one way or the other - give it the same default rock kit the
  // interactive "add-drum-machine-track" command would (seedDefaultKit()'s
  // own comment), rather than leaving a silent, lane-less track behind. A
  // real, however sparse, <drumMachine> element means the file IS being
  // explicit about its lanes (down to genuinely zero, if that's what it
  // says), so it's left alone here.
  if (!drum_machine_element) {
    track.seedDefaultKit();
    return;
  }

  auto id = drum_machine_element->Attribute("id");
  track.setSequenceId(id ? id : "");
  auto loop_length = drum_machine_element->Attribute("loop_length");
  track.setLoopLength(loop_length ? atoi(loop_length) : 8);

  for (auto it = drum_machine_element->FirstChildElement("lane"); it; it = it->NextSiblingElement("lane")) {
    auto note_text = it->Attribute("note");
    if (!note_text) continue;
    int note = atoi(note_text);
    track.addLane(note);

    uint8_t steps = 0;
    auto steps_text = it->Attribute("steps");
    if (steps_text) {
      for (int i = 0; steps_text[i] != 0 && i < 8; i++) {
        if (steps_text[i] == '1') steps = static_cast<uint8_t>(steps | (1u << i));
      }
    }
    track.setSteps(note, steps);
  }
}

static void storeDrumMachineData(const DrumMachineTrack & track, XMLDocument & doc, XMLElement * track_element) {
  auto drum_machine_element = doc.NewElement("drumMachine");
  if (!track.getSequenceId().empty()) drum_machine_element->SetAttribute("id", track.getSequenceId().c_str());
  drum_machine_element->SetAttribute("loop_length", track.getLoopLength());

  for (auto note : track.getLaneNotes()) {
    auto lane_element = doc.NewElement("lane");
    lane_element->SetAttribute("note", note);

    auto steps = track.getSteps(note);
    string steps_text(static_cast<size_t>(track.getLoopLength()), '0');
    for (int i = 0; i < track.getLoopLength(); i++) {
      if ((steps & (1u << i)) != 0) steps_text[static_cast<size_t>(i)] = '1';
    }
    lane_element->SetAttribute("steps", steps_text.c_str());

    drum_machine_element->InsertEndChild(lane_element);
  }

  track_element->InsertEndChild(drum_machine_element);
}

static std::unique_ptr<Track> parseChildTrack(XMLElement & element, const InstrumentProvider & provider) {
  auto track = createTrack(element.Name());
  if (!track) return std::unique_ptr<Track>(nullptr);

  track->loadParameters(XMLParameterSource(&element));

  auto instrument = dynamic_cast<Instrument *>(track.get());
  if (instrument) {
    instrument->prepare(provider);
  }

  auto drum_machine_track = dynamic_cast<DrumMachineTrack *>(track.get());
  if (drum_machine_track) {
    loadDrumMachineData(*drum_machine_track, element.FirstChildElement("drumMachine"));
  }

  for (auto it = element.FirstChildElement(); it ; it = it->NextSiblingElement() ) {
    if (string_view(it->Name()) == "drumMachine") continue; // data, not a nested track - handled above
    auto child = parseChildTrack(*it, provider);
    if (!child) return std::unique_ptr<Track>(nullptr);
    track->addChild(std::move(child));
  }

  return track;
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

void
Song::setBusSlotKind(int slot, BusEffectKind kind) {
  auto & descriptor = findBusEffectDescriptor(kind);
  auto effect = descriptor.factory(kPlaceholderBusSampleRate);
  if (slot == 0) { bus_slot_a_ = std::move(effect); bus_slot_a_kind_ = kind; }
  else { bus_slot_b_ = std::move(effect); bus_slot_b_kind_ = kind; }
}

std::unique_ptr<TrackState>
Song::createState(const ChannelConfiguration & config) const {
  return make_unique<SongState>(config);
}

bool
Song::open(const std::string & filename, const InstrumentProvider & provider) {
  auto oldLocale = setlocale(LC_ALL, 0);
  setlocale(LC_ALL, "C");
  
  XMLDocument doc;
  if (doc.LoadFile(filename.c_str()) != 0) {
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
      for (auto it = instruments->FirstChildElement(); it; it = it->NextSiblingElement() ) {
	auto instrument = parseChildTrack(*it, provider);
	if (instrument) {
	  addInstrument(move(instrument));
	}	
      }
    }

    auto tracks = song->FirstChildElement("tracks");
    if (tracks) {
      for (auto it = tracks->FirstChildElement(); it ; it = it->NextSiblingElement() ) {
	auto track = parseChildTrack(*it, provider);
	if (track) {
	  addTrack(move(track));
	}
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
	  auto tuning = track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : getTuning();

	  for (auto it3 = it2->FirstChildElement("note"); it3 ; it3 = it3->NextSiblingElement("note")) {
	    auto row_text = it3->Attribute("row");
	    auto column_text = it3->Attribute("column");
	    auto velocity_text = it3->Attribute("velocity");
	    auto delay_text = it3->Attribute("delay");

	    auto value_text = it3->GetText();
	    if (!value_text) value_text = it3->Attribute("value");

	    if (value_text) {
	      int row = row_text ? atoi(row_text) : 0;
	      int start_column = column_text ? atoi(column_text) : 0;
	      int velocity = velocity_text ? atoi(velocity_text) : constants::DEFAULT_VELOCITY;
	      int delay = delay_text ? atoi(delay_text) : 0;

	      auto notes = Note::createFromString(value_text, velocity, delay, tuning);
	      for (int i = 0; i < static_cast<int>(notes.size()); i++) {
		scene.setNote(row, track_id, start_column + i, notes[i]);
	      }
	    }
	  }

	  for (auto it3 = it2->FirstChildElement("command"); it3; it3 = it3->NextSiblingElement("command")) {
	    auto row_text = it3->Attribute("row");
	    auto data_text = it3->Attribute("data");

	    if (data_text) {
	      int row = row_text ? atoi(row_text) : 0;
	      Command command(data_text);
	      scene.setCommand(row, track_id, command);
	    }
	  }
	}
      }
    }
  }

  setlocale(LC_ALL, oldLocale);

  return true;
}

void
Song::save(const std::string & filename) const {
  auto oldLocale = setlocale(LC_ALL, 0);
  setlocale(LC_ALL, "C");
 
  XMLDocument doc;
  doc.InsertEndChild(doc.NewDeclaration());
    
  auto root = doc.NewElement("song");
  XMLParameterSource song_parameters(root);
  storeParameters(song_parameters);
  doc.InsertEndChild(root);

  storeBusConfig(*this, doc, root);

  auto instruments = doc.NewElement("instruments");
  root->InsertEndChild(instruments);

  auto tracks = doc.NewElement("tracks");
  root->InsertEndChild(tracks);

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
      auto track = getTrackByInternalId(track_id);
      assert(track);
      // A DrumMachineTrack's sequence lives on the track itself (see
      // DrumMachineTrack.h) - it must never end up referenced from Pattern
      // row data, since that data is silently ignored on load (parseChildTrack
      // never routes <note> elements to it, only Song::open()'s own
      // <scenes> handling does, keyed by whatever track_id happens to be
      // stored) and would otherwise be lost without any error.
      assert(!track || track->getType() != TrackType::DRUM_MACHINE);
      if (!track) continue;

      auto track_tuning = track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : getTuning();
      auto track_ref = trackReferenceText(*this, track_id);

      auto pattern_element = doc.NewElement("pattern");
      pattern_element->SetAttribute("track", track_ref.c_str());

      for (int row = 0; row < getPatternLength(); row++) {
	auto & nv = pattern.getNotes(row);

	// TODO: check if velocity and delay are same, and store notes in single element
	for (size_t col = 0; col < nv.size(); col++) {
	  auto & note = nv[col];
	  auto note_text = note.toString(track_tuning);
	  auto note_element = doc.NewElement("note");
	  note_element->SetAttribute("row", row);
	  if (col > 0) note_element->SetAttribute("column", col);
	  if (note.getVelocity() != constants::DEFAULT_VELOCITY) note_element->SetAttribute("velocity", note.getVelocity());
	  if (note.getDelay() > 0) note_element->SetAttribute("delay", note.getDelay());
	  note_element->SetText(note_text.c_str());
	  pattern_element->InsertEndChild(note_element);
	}
      }

      for (auto & [ row, command ] : pattern.getCommands() ) {
	auto data = to_string(command);

	auto command_element = doc.NewElement("command");
	command_element->SetAttribute("row", row);
	command_element->SetAttribute("data", data.c_str());
	pattern_element->InsertEndChild(command_element);
      }

      scene_element->InsertEndChild(pattern_element);
    }

    scenes->InsertEndChild(scene_element);
  }

  for (auto & track : getTracks()) {
    storeChildTrack(*track, doc, tracks);
  }

  for (auto & instrument : getInstruments()) {
    storeChildTrack(*instrument, doc, instruments);
  }
  
  doc.SaveFile(filename.c_str());

  setlocale(LC_ALL, oldLocale);
}
  
void
Song::loadParameters(const ParameterSource & input) {
  StatefulSongObject::loadParameters(input);

  auto song_tuning = parse_tuning(input.getText("temperament"), Tuning::TET12);
  setTuning(song_tuning);

  auto key_text = input.getText("key");
  if (!key_text.empty()) setKey(Note::stringToKey(song_tuning, key_text));

  setTempo(input.getInt("tempo", 90));
  setPatternLength(input.getInt("patternRows", 64));

  setEarHeight(input.getFloat("earHeight", constants::DEFAULT_EAR_HEIGHT));
  setFloorReflectionEnabled(input.getBool("floorReflection", constants::DEFAULT_FLOOR_REFLECTION_ENABLED));
  setFloorReflectionStrength(input.getFloat("floorReflectionStrength", constants::DEFAULT_FLOOR_REFLECTION_STRENGTH));
  setGroundAbsorption(input.getFloat("groundAbsorption", constants::DEFAULT_GROUND_ABSORPTION));

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
  StatefulSongObject::storeParameters(output);

  if (getKey() >= 0) output.set("key", Note::keyToString(getTuning(), getKey()));
  output.set("temperament", to_string(getTuning()));
  output.set("tempo", getTempo());
  output.set("patternRows", getPatternLength(), 64);

  output.set("earHeight", getEarHeight(), constants::DEFAULT_EAR_HEIGHT);
  if (getFloorReflectionEnabled() != constants::DEFAULT_FLOOR_REFLECTION_ENABLED) output.set("floorReflection", getFloorReflectionEnabled());
  output.set("floorReflectionStrength", getFloorReflectionStrength(), constants::DEFAULT_FLOOR_REFLECTION_STRENGTH);
  output.set("groundAbsorption", getGroundAbsorption(), constants::DEFAULT_GROUND_ABSORPTION);
}

static void
collectRootTrackIds(const Track & track, vector<int> & track_ids) {
  if (track.getType() == TrackType::INSTRUMENT_CONTROL ||
      track.getType() == TrackType::PERCUSSION_CONTROL ||
      track.getType() == TrackType::SAMPLE ||
      track.getType() == TrackType::DRUM_MACHINE) {
    track_ids.push_back(track.getInternalId());
  } else {
    for (auto & child : track.getChildren()) {
      collectRootTrackIds(*child, track_ids);
    }
  }
}

vector<int>
Song::getRootTrackIds() const {
  vector<int> track_ids;
  for (auto & child : getTracks()) {
    collectRootTrackIds(*child, track_ids);
  }
  return track_ids;
}
