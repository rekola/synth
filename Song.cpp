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

  string song_key_text;
  if (getKey() >= 0) song_key_text = Note::keyToString(getTuning(), getKey());

  XMLElement * root = doc.NewElement("song");
  if (!getName().empty()) root->SetAttribute("name", getName().c_str());
  root->SetAttribute("key", song_key_text.c_str());
  if (!getName().empty()) root->SetAttribute("name", getName().c_str());
  // root->SetAttribute("tuning", "");
  root->SetAttribute("tempo", getTempo());
  doc.InsertFirstChild(root);

  XMLElement * patterns = doc.NewElement("patterns");
  root->InsertEndChild(patterns);

  XMLElement * tracks = doc.NewElement("tracks");
  root->InsertEndChild(tracks);

  for (auto & pattern : getPatterns()) {
    Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : getTuning();

    string key_text;
    if (pattern.getKey() >= 0) key_text = Note::keyToString(tuning, pattern.getKey());
      
    XMLElement * pattern_element = doc.NewElement("pattern");
    if (!pattern.getName().empty()) pattern_element->SetAttribute("name", pattern.getName().c_str());
    if (!key_text.empty()) pattern_element->SetAttribute("key", key_text.c_str());
    if (pattern.getTuning() != Tuning::INHERIT) {
      // pattern_element->SetAttribute("tuning", "");
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
  for (auto & track : mastertrack.getChildren()) {
    XMLElement * track_element = doc.NewElement("track");
    if (!track.getName().empty()) track_element->SetAttribute("name", track.getName().c_str());
    if (track.isSolo()) track_element->SetAttribute("solo", "1");
    if (track.isMuted()) track_element->SetAttribute("mute", "1");
    tracks->InsertEndChild(track_element);    
  }
  
  doc.SaveFile(filename.c_str());
}

