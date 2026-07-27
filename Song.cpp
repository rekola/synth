#include "Song.h"

#include "SongState.h"

#include "InstrumentTrack.h"
#include "PercussionTrack.h"
#include "Group.h"
#include "NoteMultiplier.h"
#include "Arpeggiator.h"
#include "HarmonicSeries.h"
#include "Oscilator.h"
#include "Noise.h"
#include "LFO.h"
#include "GenericInstrument.h"

#include "effects/Distortion.h"
#include "effects/Reverb.h"
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
  else if (name == "group") return make_unique<Group>();

  // effects
  else if (name == "reverb") return make_unique<Reverb>();
  else if (name == "distortion") return make_unique<Distortion>();
  else if (name == "resonantFilter") return make_unique<ResonantFilter>();
  else if (name == "biquadFilter") return make_unique<BiquadFilter>();
  else if (name == "chorus") return make_unique<Chorus>();
  else if (name == "tremolo") return make_unique<Tremolo>();
  else if (name == "multiply") return make_unique<NoteMultiplier>();
  else if (name == "arpeggiator") return make_unique<Arpeggiator>();
  else if (name == "envelope") return make_unique<EnvelopeFilter>();
  else if (name == "harmonicSeries") return make_unique<HarmonicSeries>();
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

static std::unique_ptr<Track> parseChildTrack(XMLElement & element, const InstrumentProvider & provider) {
  auto track = createTrack(element.Name());
  if (!track) return std::unique_ptr<Track>(nullptr);
  
  track->loadParameters(XMLParameterSource(&element));

  auto instrument = dynamic_cast<Instrument *>(track.get());
  if (instrument) {
    instrument->prepare(provider);
  }

  for (auto it = element.FirstChildElement(); it ; it = it->NextSiblingElement() ) {
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
    
    auto patterns = song->FirstChildElement("patterns");
    if (patterns) {
      for (auto it = patterns->FirstChildElement("pattern"); it ; it = it->NextSiblingElement("pattern") ) {
	auto & pattern = addPattern(Pattern());
	pattern.loadParameters(XMLParameterSource(it));
	
	for (auto it2 = it->FirstChildElement("note"); it2 ; it2 = it2->NextSiblingElement("note")) {
	  auto track_text = it2->Attribute("track");
	  auto row_text = it2->Attribute("row");
	  auto column_text = it2->Attribute("column");
	  auto velocity_text = it2->Attribute("velocity");
	  auto delay_text = it2->Attribute("delay");

	  auto value_text = it2->GetText();
	  if (!value_text) value_text = it2->Attribute("value");	  
	  
	  if (track_text && value_text) {
	    int row = row_text ? atoi(row_text) : 0;
	    int start_column = column_text ? atoi(column_text) : 0;
	    int velocity = velocity_text ? atoi(velocity_text) : constants::DEFAULT_VELOCITY;
	    int delay = delay_text ? atoi(delay_text) : 0;

	    auto track = getTrackById(track_text);
	    if (track) {
	      auto track_id = track->getInternalId();
	      auto tuning = track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : getTuning();
	      auto notes = Note::createFromString(value_text, velocity, delay, tuning);
	      for (int i = 0; i < static_cast<int>(notes.size()); i++) {
		pattern.setNote(row, track_id, start_column + i, notes[i]);
	      }	      
	    }
	  }
	}

	for (auto it2 = it->FirstChildElement("annotation"); it2; it2 = it2->NextSiblingElement("annotation")) {
	  auto row_text = it2->Attribute("row");
	  if (row_text) {
	    int row = atoi(row_text);
	    auto s = it2->GetText();
	    pattern.setAnnotation(row, s ? s : "");
	  }
	}

	for (auto it2 = it->FirstChildElement("command"); it2; it2 = it2->NextSiblingElement("command")) {
	  auto track_text = it2->Attribute("track");
	  auto row_text = it2->Attribute("row");
	  auto data_text = it2->Attribute("data");

	  int track = track_text ? atoi(track_text) : 0;
	  int row = row_text ? atoi(row_text) : 0;

	  if (data_text) {
	    Command command(data_text);
	    pattern.setCommand(row, track, command);
	  }
	}	
      }
    }

    auto sections = song->FirstChildElement("sections");
    if (sections) {
      for (auto it = patterns->FirstChildElement("section"); it ; it = it->NextSiblingElement("section") ) {
	auto & section = addSection(Section());
	section.loadParameters(XMLParameterSource(it));

	for (auto it2 = it->FirstChildElement("pattern"); it2 ; it2 = it2->NextSiblingElement("pattern")) {
	  
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

  auto sections = doc.NewElement("sections");
  root->InsertEndChild(sections);

  for (auto & section : getSections()) {
    auto section_element = doc.NewElement("section");
    XMLParameterSource section_parameters(section_element);
    section.storeParameters(section_parameters);

    sections->InsertEndChild(section_element);
  }
  
  auto patterns = doc.NewElement("patterns");
  root->InsertEndChild(patterns);

  for (auto & pattern : getPatterns()) {     
    auto pattern_element = doc.NewElement("pattern");
    XMLParameterSource pattern_parameters(pattern_element);
    pattern.storeParameters(pattern_parameters);

    for (size_t row = 0; row < pattern.getNumRows(); row++) {
      auto & notes = pattern.getNotes(row);

      for (auto & [ track_id, nv ] : notes) {
	// TODO: check if velocity and delay are same, and store notes in single element

	auto track = getTrackByInternalId(track_id);
	assert(track);
	
	if (track) {
	  auto track_tuning = track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : getTuning();
	  
	  for (size_t col = 0; col < nv.size(); col++) {
	    auto & note = nv[col];
	    auto note_text = note.toString(track_tuning);
	    auto note_element = doc.NewElement("note");
	    note_element->SetAttribute("row", row);
	    note_element->SetAttribute("track", track_id);
	    if (col > 0) note_element->SetAttribute("column", col);
	    if (note.getVelocity() != constants::DEFAULT_VELOCITY) note_element->SetAttribute("velocity", note.getVelocity());
	    if (note.getDelay() > 0) note_element->SetAttribute("delay", note.getDelay());
	    note_element->SetText(note_text.c_str());
	    pattern_element->InsertEndChild(note_element);
	  }
	}
      }
	
      auto & commands = pattern.getCommands(row);
      for (auto & [ track_id, command ] : commands) {
	auto data = to_string(command);
	  
	auto command_element = doc.NewElement("command");
	command_element->SetAttribute("row", row);
	command_element->SetAttribute("track", track_id);
	command_element->SetAttribute("data", data.c_str());
	pattern_element->InsertEndChild(command_element);
      }

      auto & annotation = pattern.getAnnotation(row);
      if (!annotation.empty()) {
	auto annotation_element = doc.NewElement("annotation");
	annotation_element->SetAttribute("row", row);
	annotation_element->SetText(annotation.c_str());
	pattern_element->InsertEndChild(annotation_element);      
      }
    }
    
    patterns->InsertEndChild(pattern_element);
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
  setRandomizationFactor(input.getFloat("randomization", 0.01f));

  // The bus (reverb/delay/...) is not a <song> attribute - it's the
  // <bus> child element, parsed separately in Song::open() (mirroring
  // how <tracks>/<instruments>/<patterns> are handled there too, not
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
  output.set("randomization", getRandomizationFactor());
}

static void
collectRootTrackIds(const Track & track, vector<int> & track_ids) {
  if (track.getType() == TrackType::INSTRUMENT_CONTROL ||
      track.getType() == TrackType::PERCUSSION_CONTROL ||
      track.getType() == TrackType::SAMPLE) {
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
