#include "Song.h"

#include "SongState.h"
#include "TrackEventQueue.h"
#include "SampleData.h"
#include "Tuner.h"

#include "tinyxml2.h"

using namespace std;
using namespace tinyxml2;

Tuning parse_tuning(const char * tuning_text) {
  if (tuning_text && strcmp(tuning_text, "12-TET") != 0) {
    if (strcmp(tuning_text, "31-TET") == 0) {
      return Tuning::TET31;
    } else if (strcmp(tuning_text, "19-TET") == 0) {
      return Tuning::TET19;
    } else {
      assert(0);      
    }
  }
  return Tuning::TET12;
}

void
Song::open(const std::string & filename) {
  char * oldLocale = setlocale(LC_ALL, 0);
  setlocale(LC_ALL, "C");

  XMLDocument doc;
  doc.LoadFile(filename.c_str());

  auto song = doc.FirstChildElement("song");
  if (song) {   
    Tuning song_tuning = parse_tuning(song->Attribute("tuning"));
    setTuning(song_tuning);

    auto key_text = song->Attribute("key");
    setKey(key_text ? Note::stringToKey(song_tuning, key_text) : -1);

    auto volume_text = song->Attribute("volume");
    setVolume(volume_text ? atof(volume_text) : 1.0f);
    
    auto randomization_text = song->Attribute("randomization");
    setRandomizationFactor(randomization_text ? atof(randomization_text) : 1.0f);
    
    auto instruments = song->FirstChildElement("instruments");
    if (instruments) {
      auto it = instruments->FirstChildElement("instrument");
      for ( ; it ; it = it->NextSiblingElement() ) {
	// ?
      }
    }

    auto tracks = song->FirstChildElement("tracks");
    if (tracks) {
      auto it = tracks->FirstChildElement("track");
      for ( ; it ; it = it->NextSiblingElement() ) {
	auto & track = addChild();
      }
    }
    
    auto patterns = song->FirstChildElement("patterns");
    if (patterns) {
      auto it = patterns->FirstChildElement("pattern");
      for ( ; it ; it = it->NextSiblingElement() ) {
	auto it2 = it->FirstChildElement("note");

	auto rows_text = it2->Attribute("rows");
	auto tuning_text = it2->Attribute("tuning");
	auto pattern_key_text = it2->Attribute("key");

	Tuning pattern_tuning = parse_tuning(tuning_text);
	Tuning actual_pattern_tuning = pattern_tuning != Tuning::INHERIT ? pattern_tuning : song_tuning;

	int rows = rows_text ? atoi(rows_text) : 0;
	int key = pattern_key_text ? Note::stringToKey(actual_pattern_tuning, pattern_key_text) : -1;
	
	auto & pattern = addPattern(rows, pattern_tuning, key);
	
	for ( ; it2 ; it2 = it2->NextSiblingElement() ) {
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
	  pattern.setNote(track, row, column, note);
	}
      }
    }
  }
  setlocale(LC_ALL, oldLocale);
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
  root->SetAttribute("key", song_key_text.c_str());
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
      string tuning_text = to_string(getTuning());
      pattern_element->SetAttribute("tuning", tuning_text.c_str());
    }

    for (size_t row = 0; row < pattern.getNumRows(); row++) {
      for (size_t track = 0; track < getChildren().size(); track++) {
	auto & nv = pattern.getNotes(track, row);
	for (size_t col = 0; col < nv.size(); col++) {
	  auto & note = nv[col];
	  auto note_text = note.toString(tuning);
	  XMLElement * note_element = doc.NewElement("note");
	  note_element->SetAttribute("track", track);
	  note_element->SetAttribute("row", row);
	  if (col > 0) note_element->SetAttribute("column", col);
	  note_element->SetAttribute("velocity", note.getVelocity());
	  note_element->SetAttribute("value", note_text.c_str());
	  pattern_element->InsertEndChild(note_element);
  	}
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
    if (!track.getName().empty()) track_element->SetAttribute("name", track.getName().c_str());
    if (track.isSolo()) track_element->SetAttribute("solo", "1");
    if (track.isMuted()) track_element->SetAttribute("mute", "1");
    track_element->SetAttribute("azimuth", track.getAzimuth());
    track_element->SetAttribute("distance", track.getDistance());
    track_element->SetAttribute("elevation", track.getElevation());
    track_element->SetAttribute("volume", track.getVolume());
    if (track.getDetune() != 0) track_element->SetAttribute("detune", track.getDetune());
    track_element->SetAttribute("instrument", track.getInstrumentId());
    
    tracks->InsertEndChild(track_element);    
  }

  for (auto & instrument : getInstruments()) {
    XMLElement * instrument_element = doc.NewElement("instrument");
    if (!instrument->getName().empty()) instrument_element->SetAttribute("name", instrument->getName().c_str());
    instruments->InsertEndChild(instrument_element);    
  }
  
  doc.SaveFile(filename.c_str());

  setlocale(LC_ALL, oldLocale);
}

SampleData
Song::render(size_t frames, SongState & state) {
  auto & tracks = getChildren();

  Tuner tuner;
  TrackEventQueue track_events;
  
  if (state.isPlaying()) {
    for (size_t i = 0; i < frames; i++) {
      if (state.getSamplePos() == 0) {
	auto & pattern = getPattern(state.getPatternPosition());
	auto tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : getTuning();
	int key = pattern.getKey() >= 0 ? pattern.getKey() : getKey();

	for (size_t col = 0; col < tracks.size(); col++) {
	  auto & notes = pattern.getNotes(col, state.getTrackPosition());
	  for (size_t j = 0; j < notes.size(); j++) {
	    if (notes[j].isDefined()) {
	      auto & note = notes[j];
	      float frequency, velocity;
	      if (note.isOff()) {
		frequency = velocity = 0.0f;
	      } else {
		frequency = tuner.getFrequency(tuning, key, note);
		velocity = note.getVelocityAsFloat();
	      }
	      float delay = 0; // getRandomizationFactor() * samplerate * rand() / RAND_MAX;
	      track_events.addPendingEvent(col, i, int(j), delay, frequency, velocity);
	    }
	  }
	}
      }

      size_t remaining = state.samplesUntilNextRow(*this);
      if (i + remaining <= frames) {
	i += remaining;
	state.moveForward(*this);
      } else {
	for (; i < frames; i++) {
	  state.moveForwardSample(*this);
	}
      }
    }
  }
  
  auto & mixer = state.getMixer();
  mixer.reset();
  
  for (size_t track_idx = 0; track_idx < tracks.size(); track_idx++) {
    auto & track = tracks[track_idx];
    auto & instrument = getInstrument(track.getInstrumentId());
    auto & track_state = state.getTrackState(track_idx);

    SampleData data = track.render(frames, track_state, instrument, track_events.getPendingEvents(track_idx));
    mixer.accumulate(data, track.getVolume(), track.getDistance(), track.getAzimuth(), track.getElevation());
  }
  assert(track_events.empty());

  SampleData master(2, frames);
  
  mixer.encode(master, getVolume());
  // applyEffects(master);
  
  return master;
}

