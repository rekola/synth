
#include "Song.h"

#include "SongState.h"
#include "SampleData.h"

#include "InstrumentTrack.h"
#include "Group.h"
#include "NoteMultiplier.h"
#include "EnvelopeFilter.h"

#include "effects/Distortion.h"
#include "effects/Reverb.h"
#include "effects/Filter.h"
#include "effects/Compressor.h"
#include "effects/Delay.h"
#include "effects/Chorus.h"
#include "effects/Tremolo.h"

#include "Oscilator.h"
#include "GenericInstrument.h"
#include "Mixer.h"

#include "tinyxml2.h"

using namespace std;
using namespace tinyxml2;

class XMLParameterSource : public ParameterSource {
public:
  XMLParameterSource(XMLElement * element) : element_(element) { }

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

static inline vector<string> split_notes(const string & line) {
  vector<string> r;
  
  if (!line.empty()) {
    size_t i0 = 0, i = 0;
    for ( ; i < line.size(); i++) {
      if (isspace(line[i])) {
	r.push_back(line.substr(i0, i - i0));
	while (isspace(line[i + 1])) i++;
	i0 = i + 1;
      }
    }
    r.push_back(line.substr(i0, i - i0));
  }
  return r;
}

Tuning parse_tuning(const char * tuning_text, Tuning default_tuning = Tuning::INHERIT) {
  if (tuning_text) {
    if (strcmp(tuning_text, "12edo") == 0) {
      return Tuning::TET12;
    } else if (strcmp(tuning_text, "31edo") == 0) {
      return Tuning::TET31;
    } else if (strcmp(tuning_text, "19edo") == 0) {
      return Tuning::TET19;
    } else if (strcmp(tuning_text, "53edo") == 0) {
      return Tuning::TET53;
    } else {
      assert(0);
    }
  }
  return default_tuning;
}

static unique_ptr<Track> createTrack(string name) {  
  if (name == "track") return make_unique<InstrumentTrack>();
  else if (name == "group") return make_unique<Group>();

  // effects
  else if (name == "reverb") return make_unique<Reverb>();
  else if (name == "distortion") return make_unique<Distortion>();
  else if (name == "filter") return make_unique<Filter>();
  else if (name == "compressor") return make_unique<Compressor>();
  else if (name == "delay") return make_unique<Delay>();
  else if (name == "chorus") return make_unique<Chorus>();
  else if (name == "tremolo") return make_unique<Tremolo>();
  else if (name == "multiply") return make_unique<NoteMultiplier>();
  else if (name == "envelope") return make_unique<EnvelopeFilter>();
  
  // instruments
  else if (name == "genericInstrument") return make_unique<GenericInstrument>();
  else if (name == "oscilator") return make_unique<Oscilator>(WaveformType::SAW);

  else {
    assert(0);
    return unique_ptr<Track>(nullptr);
  }
}

static void parseChildTrack(Track & parent, XMLElement & element) {
  auto & track = parent.addChild(createTrack(element.Name()));
  track.loadParameters(XMLParameterSource(&element));
  
  for (auto it = element.FirstChildElement(); it ; it = it->NextSiblingElement() ) {
    parseChildTrack(track, *it);
  }
}

static void parseChildInstrument(Track & parent, XMLElement & element, const InstrumentProvider & provider) {
  auto track = createTrack(element.Name());
  if (!track) return;

  track->loadParameters(XMLParameterSource(&element));

  auto * instrument = dynamic_cast<Instrument *>(track.get());
  if (instrument) {
    instrument->prepare(provider);
  }

  for (auto it = element.FirstChildElement(); it ; it = it->NextSiblingElement() ) {
    parseChildInstrument(*track, *it, provider);
  }

  Song * song = dynamic_cast<Song *>(&parent);
  if (song) {
    song->addInstrument(move(track));
  } else {
    parent.addChild(move(track));
  }
}  
		      
bool
Song::open(const std::string & filename, const InstrumentProvider & provider) {
  char * oldLocale = setlocale(LC_ALL, 0);
  setlocale(LC_ALL, "C");
  
  XMLDocument doc;
  if (doc.LoadFile(filename.c_str()) != 0) {
    return false;
  }

  auto song = doc.FirstChildElement("song");
  if (song) {   
    Tuning song_tuning = parse_tuning(song->Attribute("temperament"), Tuning::TET12);
    setTuning(song_tuning);

    auto key_text = song->Attribute("key");
    if (key_text && strlen(key_text) > 0) setKey(Note::stringToKey(song_tuning, key_text));

    auto tempo_text = song->Attribute("tempo");
    if (tempo_text) setTempo(atoi(tempo_text));
    
    auto volume_text = song->Attribute("volume");
    if (volume_text) setVolume(strtof(volume_text, nullptr));
    
    auto randomization_text = song->Attribute("randomization");
    if (randomization_text) setRandomizationFactor(strtof(randomization_text, nullptr));

    auto mixer_text = song->Attribute("mixer");
    if (mixer_text) {
      if (strcmp(mixer_text, "basic") == 0) setMixerType(MixerType::BASIC);
      else if (strcmp(mixer_text, "hrft") == 0) setMixerType(MixerType::HRFT);
    }
    
    auto instruments = song->FirstChildElement("instruments");
    if (instruments) {
      for (auto it = instruments->FirstChildElement(); it; it = it->NextSiblingElement() ) {
	parseChildInstrument(*this, *it, provider);
      }
    }

    auto tracks = song->FirstChildElement("tracks");
    if (tracks) {
      for (auto it = tracks->FirstChildElement(); it ; it = it->NextSiblingElement() ) {
	parseChildTrack(*this, *it);	
      }
    }
    
    auto patterns = song->FirstChildElement("patterns");
    if (patterns) {
      for (auto it = patterns->FirstChildElement("pattern"); it ; it = it->NextSiblingElement("pattern") ) {
	auto rows_text = it->Attribute("rows");
	auto tuning_text = it->Attribute("temperament");
	auto pattern_key_text = it->Attribute("key");

	Tuning pattern_tuning = parse_tuning(tuning_text);
	Tuning actual_pattern_tuning = pattern_tuning != Tuning::INHERIT ? pattern_tuning : song_tuning;

	int rows = rows_text ? atoi(rows_text) : 0;
	int key = pattern_key_text && strlen(pattern_key_text) > 0 ? Note::stringToKey(actual_pattern_tuning, pattern_key_text) : -1;
	
	auto & pattern = addPattern(rows, pattern_tuning, key);

	for (auto it2 = it->FirstChildElement("note"); it2 ; it2 = it2->NextSiblingElement("note")) {
	  auto track_text = it2->Attribute("track");
	  auto row_text = it2->Attribute("row");
	  auto column_text = it2->Attribute("column");
	  auto velocity_text = it2->Attribute("velocity");
	  auto delay_text = it2->Attribute("delay");

	  auto value_text = it2->GetText();
	  if (!value_text) value_text = it2->Attribute("value");
	  
	  int track = track_text ? atoi(track_text) : 0;
	  int row = row_text ? atoi(row_text) : 0;
	  int start_column = column_text ? atoi(column_text) : 0;
	  int velocity = velocity_text ? atoi(velocity_text) : 0;
	  int delay = delay_text ? atoi(delay_text) : 0;

	  auto notes = split_notes(value_text);

	  for (int i = 0; i < static_cast<int>(notes.size()); i++) {
	    if (notes[i] == "off" || notes[i] == "OFF") {
	      pattern.setNote(row, track, start_column + i, Note(0, 0));
	    } else {
	      pattern.setNote(row, track, start_column + i, Note(notes[i], velocity, delay, actual_pattern_tuning));
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
  }

  setlocale(LC_ALL, oldLocale);

  return true;
}

void
Song::save(const std::string & filename) const {
  char * oldLocale = setlocale(LC_ALL, 0);
  setlocale(LC_ALL, "C");
 
  XMLDocument doc;
  doc.InsertEndChild(doc.NewDeclaration());
  
  string song_tuning_text = to_string(getTuning());
  string song_key_text;
  if (getKey() >= 0) song_key_text = Note::keyToString(getTuning(), getKey());
  string mixer_text = to_string(getMixerType());
  
  XMLElement * root = doc.NewElement("song");
  if (!getName().empty()) root->SetAttribute("name", getName().c_str());
  if (!song_key_text.empty()) root->SetAttribute("key", song_key_text.c_str());
  if (!getName().empty()) root->SetAttribute("name", getName().c_str());
  root->SetAttribute("temperament", song_tuning_text.c_str());
  root->SetAttribute("tempo", getTempo());
  root->SetAttribute("volume", getVolume());
  root->SetAttribute("randomization", getRandomizationFactor());
  root->SetAttribute("mixer", mixer_text.c_str());
  doc.InsertEndChild(root);

  XMLElement * instruments = doc.NewElement("instruments");
  root->InsertEndChild(instruments);

  XMLElement * tracks = doc.NewElement("tracks");
  root->InsertEndChild(tracks);
  
  XMLElement * patterns = doc.NewElement("patterns");
  root->InsertEndChild(patterns);

  for (auto & pattern : getPatterns()) {
    Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : getTuning();

    string key_text;
    if (pattern.getKey() >= 0) key_text = Note::keyToString(tuning, pattern.getKey());
      
    XMLElement * pattern_element = doc.NewElement("pattern");
    pattern_element->SetAttribute("rows", pattern.getNumRows());
    if (!pattern.getName().empty()) pattern_element->SetAttribute("name", pattern.getName().c_str());
    if (!key_text.empty()) pattern_element->SetAttribute("key", key_text.c_str());
    if (pattern.getTuning() != Tuning::INHERIT) {
      string tuning_text = to_string(pattern.getTuning());
      pattern_element->SetAttribute("temperament", tuning_text.c_str());
    }

    for (size_t row = 0; row < pattern.getNumRows(); row++) {
      auto & notes = pattern.getNotes(row);

      for (auto & [ track_id, nv ] : notes) {
	// TODO: check if velocity and delay are same, and store notes in single element
	
	for (size_t col = 0; col < nv.size(); col++) {
	  auto & note = nv[col];
	  auto note_text = to_string(note, tuning);
	  XMLElement * note_element = doc.NewElement("note");
	  note_element->SetAttribute("row", row);
	  note_element->SetAttribute("track", track_id);
	  if (col > 0) note_element->SetAttribute("column", col);
	  note_element->SetAttribute("velocity", note.getVelocity());
	  if (note.getDelay() > 0) note_element->SetAttribute("delay", note.getDelay());
	  note_element->SetText(note_text.c_str());
	  pattern_element->InsertEndChild(note_element);
	}
      }
	
      auto & commands = pattern.getCommands(row);
      for (auto & [ track_id, command ] : commands) {
	auto data = to_string(command);
	  
	XMLElement * command_element = doc.NewElement("command");
	command_element->SetAttribute("row", row);
	command_element->SetAttribute("track", track_id);
	command_element->SetAttribute("data", data.c_str());
	pattern_element->InsertEndChild(command_element);
      }

      auto & annotation = pattern.getAnnotation(row);
      if (!annotation.empty()) {
	XMLElement * annotation_element = doc.NewElement("annotation");
	annotation_element->SetAttribute("row", row);
	annotation_element->SetText(annotation.c_str());
	pattern_element->InsertEndChild(annotation_element);      
      }
    }
    
    patterns->InsertEndChild(pattern_element);
  }

  for (auto & track : getChildren()) {
    XMLElement * track_element = doc.NewElement("track");
    XMLParameterSource parameters(track_element);
    track->storeParameters(parameters);
    
    tracks->InsertEndChild(track_element);    
  }

  for (auto & instrument : getInstruments()) {
    auto name = instrument->getElementName();
    auto instrument_element = doc.NewElement(name.c_str());
    XMLParameterSource parameters(instrument_element);
    instrument->storeParameters(parameters);
    instruments->InsertEndChild(instrument_element);
  }
  
  doc.SaveFile(filename.c_str());

  setlocale(LC_ALL, oldLocale);
}

void
Song::render(int frames, SongState & state, Mixer & mixer) {
  mixer.reset();

  auto & track_events = state.getEventQueue();
  
  if (state.isPlaying()) {
    for (size_t i = 0; i < frames; i++) {
      if (state.getSamplePos() == 0) {
	auto [ pattern_idx, row_idx ] = state.getRelativePosition(*this);
	auto & pattern = getPattern(pattern_idx);
	auto tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : getTuning();
	auto key = pattern.getKey() >= 0 ? pattern.getKey() : getKey();

	auto & notes = pattern.getNotes(row_idx);

	if (key >= 0) {
	  state.getTuner().tune(tuning, key, notes);
	}
	
	for (auto & [ track_id, notes ] : notes) {
	  for (size_t j = 0; j < notes.size(); j++) {
	    if (notes[j].isDefined()) {
	      auto & note = notes[j];
	      float frequency = 0.0f, velocity = 0.0f;
	      float delay = 0;
	      if (note.isAftertouch()) {
		velocity = note.getVelocityAsFloat();
	      } else if (!note.isOff()) {
		frequency = state.getTuner().getFrequency(tuning, key, note);
		velocity = note.getVelocityAsFloat() * (1 + getRandomizationFactor() * rand() / RAND_MAX);
		delay = note.getDelayAsFloat();
	      }
	      delay += getRandomizationFactor() * rand() / RAND_MAX;
	      auto delay_samples = int(delay * getSampleInterval(state.getChannelConfiguration().getAudioOutSampleRate()));
	      // delay_samples = 0;
	      track_events.addPendingEvent(track_id, i + delay_samples, int(j), frequency, velocity);
	    }
	  }
	}
	auto & commands = pattern.getCommands(row_idx);
	for (auto & [ track_id, command ] : commands) {
	  // track_events.addPendingEvent(col, i, command);
	}
      }

      auto remaining = state.samplesUntilNextRow(*this);
      if (i + remaining <= frames) {
	i += remaining;
	state.movePosition(1);
      } else {
	i += frames;
	state.moveForwardSamples(*this, frames);
      }
    }
  }

  if (!getChildren().empty() && !instruments.empty()) {
    for (auto & track : getChildren()) {
      auto data = track->render(frames, state, instruments, track_events);
      if (!track->isMuted()) {
	mixer.accumulate(data, track->getVolume());
      }
    }
  }

  track_events.updateFrameOffset(-frames);
}
  
