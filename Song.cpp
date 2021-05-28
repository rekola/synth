#include "Song.h"

#include "SongState.h"
#include "TrackEventQueue.h"
#include "SampleData.h"
#include "Tuner.h"

#include "InstrumentTrack.h"
#include "GroupTrack.h"
#include "Reverb.h"
#include "Distortion.h"
#include "Filter.h"
#include "Compressor.h"
#include "Delay.h"
#include "Chorus.h"

#include "SubtractiveInstrument.h"
#include "GenericInstrument.h"

#include "tinyxml2.h"

using namespace std;
using namespace tinyxml2;

Tuning parse_tuning(const char * tuning_text, Tuning default_tuning = Tuning::INHERIT) {
  if (tuning_text) {
    if (strcmp(tuning_text, "12-TET") == 0) {
      return Tuning::TET12;
    } else if (strcmp(tuning_text, "31-TET") == 0) {
      return Tuning::TET31;
    } else if (strcmp(tuning_text, "19-TET") == 0) {
      return Tuning::TET19;
    } else {
      assert(0);
    }
  }
  return default_tuning;
}

static unique_ptr<Track> createTrack(string name) {
  if (name == "track") return make_unique<InstrumentTrack>();
  else if (name == "reverb") return make_unique<Reverb>();
  else if (name == "distortion") return make_unique<Distortion>();
  else if (name == "filter") return make_unique<Filter>();
  else if (name == "compressor") return make_unique<Compressor>();
  else if (name == "group") return make_unique<GroupTrack>();
  else if (name == "delay") return make_unique<Delay>();
  else if (name == "chorus") return make_unique<Chorus>();
  else {
    assert(0);
    return unique_ptr<Track>(nullptr);
  }
}

static void parseChildTrack(Track & parent, XMLElement & element) {
  auto & track = parent.addChild(createTrack(element.Name()));
  track.readXML(element);
  
  for (auto it = element.FirstChildElement(); it ; it = it->NextSiblingElement() ) {
    parseChildTrack(track, *it);
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
    Tuning song_tuning = parse_tuning(song->Attribute("tuning"), Tuning::TET12);
    setTuning(song_tuning);

    auto key_text = song->Attribute("key");
    if (key_text && strlen(key_text) > 0) setKey(Note::stringToKey(song_tuning, key_text));

    auto tempo_text = song->Attribute("tempo");
    if (tempo_text) setTempo(atoi(tempo_text));
    
    auto volume_text = song->Attribute("volume");
    if (volume_text) setVolume(atof(volume_text));
    
    auto randomization_text = song->Attribute("randomization");
    if (randomization_text) setRandomizationFactor(atof(randomization_text));
    
    auto instruments = song->FirstChildElement("instruments");
    if (instruments) {
      for (auto it = instruments->FirstChildElement(); it; it = it->NextSiblingElement() ) {
	string tag_name = it->Name();
	if (tag_name == "genericInstrument") {
	  auto name = it->Attribute("name");
	  if (name) addInstrument(make_unique<GenericInstrument>(name, provider));
	  else addInstrument(make_unique<GenericInstrument>(provider));
	} else if (tag_name == "fmInstrument") {
#if 0
	  auto instrument = make_unique<FMInstrument>();
#endif	  
	} else if (tag_name == "subtractiveInstrument") {
#if 0	  
	  auto instrument = make_unique<SubtractiveInstrument>();
#endif	  
	}
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
	auto tuning_text = it->Attribute("tuning");
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
	  auto value_text = it2->Attribute("value");

	  int track = track_text ? atoi(track_text) : 0;
	  int row = row_text ? atoi(row_text) : 0;
	  int column = column_text ? atoi(column_text) : 0;
	  int velocity = velocity_text ? atoi(velocity_text) : 0;
	  
	  Note note(value_text, velocity, actual_pattern_tuning);
	  pattern.setNote(row, track, column, note);
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

  XMLElement * root = doc.NewElement("song");
  if (!getName().empty()) root->SetAttribute("name", getName().c_str());
  if (!song_key_text.empty()) root->SetAttribute("key", song_key_text.c_str());
  if (!getName().empty()) root->SetAttribute("name", getName().c_str());
  root->SetAttribute("tuning", song_tuning_text.c_str());
  root->SetAttribute("tempo", getTempo());
  root->SetAttribute("volume", getVolume());
  root->SetAttribute("randomization", getRandomizationFactor());
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
      pattern_element->SetAttribute("tuning", tuning_text.c_str());
    }

    for (size_t row = 0; row < pattern.getNumRows(); row++) {
      auto & notes = pattern.getNotes(row);

      for (auto & [ track_id, nv ] : notes) {
	for (size_t col = 0; col < nv.size(); col++) {
	  auto & note = nv[col];
	  auto note_text = note.toString(tuning);
	  XMLElement * note_element = doc.NewElement("note");
	  note_element->SetAttribute("row", row);
	  note_element->SetAttribute("track", track_id);
	  if (col > 0) note_element->SetAttribute("column", col);
	  note_element->SetAttribute("velocity", note.getVelocity());
	  note_element->SetAttribute("value", note_text.c_str());
	  pattern_element->InsertEndChild(note_element);
	}
      }
	
      auto & commands = pattern.getCommands(row);
      for (auto & [ track_id, command ] : commands) {
	string data = command.toString();	  
	  
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
    track->populateXML(*track_element);
    
    tracks->InsertEndChild(track_element);    
  }

  for (auto & instrument : getInstruments()) {
    auto instrument_element = instrument->createXML(doc);
    if (instrument_element) {
      instruments->InsertEndChild(instrument_element);
    }
  }
  
  doc.SaveFile(filename.c_str());

  setlocale(LC_ALL, oldLocale);
}

SampleData
Song::render(size_t frames, SongState & state) {
  Tuner tuner;
  TrackEventQueue track_events;
  
  if (state.isPlaying()) {
    for (size_t i = 0; i < frames; i++) {
      if (state.getSamplePos() == 0) {
	auto [ pattern_idx, row_idx ] = state.getRelativePosition(*this);
	auto & pattern = getPattern(pattern_idx);
	auto tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : getTuning();
	int key = pattern.getKey() >= 0 ? pattern.getKey() : getKey();

	auto & notes = pattern.getNotes(row_idx);
	for (auto & [ track_id, notes ] : notes) {
	  for (size_t j = 0; j < notes.size(); j++) {
	    if (notes[j].isDefined()) {
	      auto & note = notes[j];
	      float frequency, velocity;
	      if (note.isOff()) {
		frequency = velocity = 0.0f;
	      } else {
		frequency = tuner.getFrequency(tuning, key, note);
		velocity = note.getVelocityAsFloat() * (1 + getRandomizationFactor() * rand() / RAND_MAX);
	      }
	      float delay = getRandomizationFactor() * rand() / RAND_MAX;
	      track_events.addPendingEvent(TrackEvent::PLAY_NOTE, track_id, i, int(j), delay, frequency, velocity);
	    }
	  }
	}
	auto & commands = pattern.getCommands(row_idx);
	for (auto & [ track_id, command ] : commands) {
	  // track_events.addPendingEvent(col, i, command);
	}
      }

      size_t remaining = state.samplesUntilNextRow(*this);
      if (i + remaining <= frames) {
	i += remaining;
	state.moveForward();
      } else {
	i += frames;
	state.moveForwardSamples(*this, frames);
      }
    }
  }

  return render(frames, state, instruments, track_events);
}

SampleData
Song::render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) {
  auto & mixer = song_state.getMixer();
  mixer.reset();

  SampleData master(2, frames);

  if (!getChildren().empty() && !instruments.empty()) {
    for (auto & track : getChildren()) {
      SampleData data = track->render(frames, song_state, instruments, events);
      mixer.accumulate(data, track->getVolume(), track->getDistance(), track->getAzimuth(), track->getElevation());
    }
    assert(events.empty());
    
    mixer.encode(master, getVolume());
    // applyEffects(master);
  }
  
  return master;
}
  
