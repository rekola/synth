#include "Song.h"

#include "SongState.h"
#include "SampleData.h"

#include "InstrumentTrack.h"
#include "PercussionTrack.h"
#include "Group.h"
#include "NoteMultiplier.h"
#include "Arpeggiator.h"
#include "EnvelopeFilter.h"

#include "effects/Distortion.h"
#include "effects/Reverb.h"
#include "effects/Filter.h"
#include "effects/Compressor.h"
#include "effects/Delay.h"
#include "effects/Chorus.h"
#include "effects/Tremolo.h"
#include "effects/Downmix.h"

#include "Oscilator.h"
#include "LFO.h"
#include "GenericInstrument.h"

#include "tinyxml2.h"

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

static inline vector<string> split_notes(string_view line) {
  vector<string> r;
  
  if (!line.empty()) {
    size_t i0 = 0, i = 0;
    for ( ; i < line.size(); i++) {
      if (isspace(line[i])) {
	r.push_back(string(line.substr(i0, i - i0)));
	while (isspace(line[i + 1])) i++;
	i0 = i + 1;
      }
    }
    r.push_back(string(line.substr(i0, i - i0)));
  }
  return r;
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
  else if (name == "group") return make_unique<Group>();

  // effects
  else if (name == "reverb") return make_unique<Reverb>();
  else if (name == "distortion") return make_unique<Distortion>();
  else if (name == "filter") return make_unique<Filter>();
  else if (name == "compressor") return make_unique<Compressor>();
  else if (name == "delay") return make_unique<Delay>();
  else if (name == "chorus") return make_unique<Chorus>();
  else if (name == "tremolo") return make_unique<Tremolo>();
  else if (name == "downmix") return make_unique<Downmix>();
  else if (name == "multiply") return make_unique<NoteMultiplier>();
  else if (name == "arpeggiator") return make_unique<Arpeggiator>();
  else if (name == "envelope") return make_unique<EnvelopeFilter>();
  
  // instruments
  else if (name == "genericInstrument") return make_unique<GenericInstrument>();
  else if (name == "oscilator") return make_unique<Oscilator>(WaveformType::SAW);
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
    loadParameters(XMLParameterSource(song));
    
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
	  
	  int row = row_text ? atoi(row_text) : 0;
	  int start_column = column_text ? atoi(column_text) : 0;
	  int velocity = velocity_text ? atoi(velocity_text) : 0;
	  int delay = delay_text ? atoi(delay_text) : 0;

	  if (track_text) {
	    auto track = getTrackById(track_text);
	    if (track) {
	      auto track_id = track->getInternalId();
	      auto tuning = track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : getTuning();
	      auto notes = split_notes(value_text);

	      for (int i = 0; i < static_cast<int>(notes.size()); i++) {
		if (notes[i] == "off" || notes[i] == "OFF") {
		  pattern.setNote(row, track_id, start_column + i, Note(0, 0));
		} else {
		  pattern.setNote(row, track_id, start_column + i, Note(notes[i], velocity, delay, tuning));
		}
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

  auto instruments = doc.NewElement("instruments");
  root->InsertEndChild(instruments);

  auto tracks = doc.NewElement("tracks");
  root->InsertEndChild(tracks);
  
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
	    note_element->SetAttribute("velocity", note.getVelocity());
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
  SongObject::loadParameters(input);
    
  auto song_tuning = parse_tuning(input.getText("temperament"), Tuning::TET12);
  setTuning(song_tuning);

  auto key_text = input.getText("key");
  if (!key_text.empty()) setKey(Note::stringToKey(song_tuning, key_text));

  setTempo(input.getInt("tempo", 90));
  setVolume(input.getFloat("volume", 1.0f));
  setRandomizationFactor(input.getFloat("randomization", 0.01f));

  auto mixer_text = input.getText("mixer");
  if (mixer_text == "basic") setMixerType(MixerType::BASIC);
  else if (mixer_text == "hrft") setMixerType(MixerType::HRFT);
}

void
Song::storeParameters(ParameterSource & output) const {
  SongObject::storeParameters(output);

  if (getKey() >= 0) output.set("key", Note::keyToString(getTuning(), getKey()));
  output.set("temperament", to_string(getTuning()));
  output.set("tempo", getTempo());
  output.set("randomization", getRandomizationFactor());
  output.set("mixer", to_string(getMixerType())); 
}
