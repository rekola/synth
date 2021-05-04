#include "Song.h"

#include "tinyxml2.h"

using namespace std;
using namespace tinyxml2;

void
Song::open(const std::string & filename) {
  XMLDocument doc;
  doc.LoadFile(filename.c_str());

  XMLElement * song = doc.FirstChildElement("song");
  assert(song);
 
}

void
Song::save(const std::string & filename) const {
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
  doc.InsertFirstChild(root);

  XMLElement * tracks = doc.NewElement("tracks");
  root->InsertEndChild(tracks);

  XMLElement * instruments = doc.NewElement("instruments");
  root->InsertEndChild(instruments);
  
  XMLElement * patterns = doc.NewElement("patterns");
  root->InsertEndChild(patterns);

  for (auto & pattern : getPatterns()) {
    Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : getTuning();

    string key_text;
    if (pattern.getKey() >= 0) key_text = Note::keyToString(tuning, pattern.getKey());
      
    XMLElement * pattern_element = doc.NewElement("pattern");
    if (!pattern.getName().empty()) pattern_element->SetAttribute("name", pattern.getName().c_str());
    if (!key_text.empty()) pattern_element->SetAttribute("key", key_text.c_str());
    if (pattern.getTuning() != Tuning::INHERIT) {
      string tuning_text = to_string(getTuning());
      pattern_element->SetAttribute("tuning", tuning_text.c_str());
    }

    auto notes = pattern.getNotes();
    for (auto & d0 : notes) {
      auto track = d0.first;
      for (auto & d1 : d0.second) {
	auto row = d1.first;
	auto & nv = d1.second;
	for (size_t i = 0; i < nv.size(); i++) {
	  auto & note = nv[i];
	  auto note_text = note.toString(tuning);
	  XMLElement * note_element = doc.NewElement("note");
	  note_element->SetAttribute("track", track);
	  note_element->SetAttribute("row", row);
	  note_element->SetAttribute("column", i);
	  note_element->SetAttribute("velocity", note.getVelocity());
	  note_element->SetAttribute("value", note_text.c_str());
	  pattern_element->InsertEndChild(note_element);
  	}
      }
    }
    
    patterns->InsertEndChild(pattern_element);
  }

  auto & mastertrack = getMasterTrack();

  XMLElement * master_element = doc.NewElement("track");
  tracks->InsertEndChild(master_element);  

  for (auto & track : mastertrack.getChildren()) {
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
    
    master_element->InsertEndChild(track_element);    
  }

  for (auto & instrument : getInstruments()) {
    XMLElement * instrument_element = doc.NewElement("instrument");
    if (!instrument->getName().empty()) instrument_element->SetAttribute("name", instrument->getName().c_str());
    instruments->InsertEndChild(instrument_element);    
  }
  
  doc.SaveFile(filename.c_str());
}

