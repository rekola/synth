
#ifndef _SONG_H_
#define _SONG_H_

#include "StatefulSongObject.h"
#include "Track.h"
#include "Section.h"
#include "Pattern.h"
#include "InstrumentSet.h"
#include "MixerType.h"

#include <memory>
#include <vector>

class SongState;
class InstrumentProvider;
class Mixer;

class Song : public StatefulSongObject {
 public:
  Song(Tuning _tuning = Tuning::TET12, short _key = -1, float _randomization_factor = 0.01f) : tuning(_tuning), key_note_number(_key), randomization_factor(_randomization_factor) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const;
  
  Tuning getTuning() const { return tuning; }
  void setTuning(Tuning _tuning) { tuning = _tuning; }

  MixerType getMixerType() const { return mixer_type; }
  void setMixerType(MixerType _mixer_type) { mixer_type = _mixer_type; }
  
  short getKey() const { return key_note_number; }
  void setKey(int key) { key_note_number = key; }
  
  float getRandomizationFactor() const { return randomization_factor; }
  void setRandomizationFactor(float f) { randomization_factor = f; }
  const std::string & getName() const { return name; }
  
  short getTempo() const { return bpm; }
  void setTempo(short _bpm) { bpm = _bpm; }
  
  const std::vector<Pattern> & getPatterns() const { return patterns; }
  const Pattern & getPattern(int i) const { return i >= 0 && i < static_cast<int>(patterns.size()) ? patterns[i] : empty_pattern; }
  Pattern & getPattern(int i) { return i >= 0 && i < static_cast<int>(patterns.size()) ? patterns[i] : empty_pattern; }

  std::pair<int, int> normalizePosition(int pattern_idx, int row_idx) const {
    while (pattern_idx < static_cast<int>(patterns.size())) {
      auto & pattern = patterns[pattern_idx];
      if (row_idx < pattern.getNumRows()) {
	break;
      } else {
	pattern_idx++;
	row_idx -= pattern.getNumRows();
      }
    }
    return std::pair(pattern_idx, row_idx);
  }
  
  Section & addSection(Section section) {
    incVersion();
    sections.push_back(std::move(section));
    return sections.back();
  }

  Pattern & addPattern(Pattern pattern) {
    incVersion();
    patterns.push_back(std::move(pattern));
    return patterns.back();
  }

  Pattern & addPattern(int rows) { return addPattern(Pattern(rows)); }
    
  const std::vector<std::unique_ptr<Track> > & getInstruments() const { return instruments; }
  const Track & getInstrument(int i) const { return *(instruments[i]); }
  void addInstrument(std::unique_ptr<Track> i) {
    instruments.push_back(std::move(i));
    incVersion();
  }

  void incVersion() { version++; }
  int getVersion() const { return version; }

  bool open(const std::string & filename, const InstrumentProvider & provider);
  void save(const std::string & filename) const;

  int getSampleInterval(int outSampleRate) const {
    return 60.0 / 4.0 / getTempo() * outSampleRate;
  }

  std::vector<std::unique_ptr<Track> > & getTracks() { return tracks; }
  const std::vector<std::unique_ptr<Track> > & getTracks() const { return tracks; }

  Track & addTrack(std::unique_ptr<Track> track) {
    tracks.push_back(std::move(track));
    incVersion();
    return *(tracks.back());
  }

  const Track * getTrackByInternalId(int id) const {
    for (auto & track : tracks) {
      auto r = track->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getTrackByInternalId(int id) {
    for (auto & track : tracks) {
      auto r = track->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  const Track * getTrackById(std::string_view id) const {
    for (auto & track : tracks) {
      auto r = track->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getTrackById(std::string_view id) {
    for (auto & track : tracks) {
      auto r = track->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  int getTrackDepth() const {
    int max_depth = 0;  
    for (auto & track : getTracks()) {
      auto d = track->getDepth();
      if (d > max_depth) max_depth = d;
    }
    return max_depth;
  }

private:
  Tuning tuning = Tuning::TET12;
  MixerType mixer_type = MixerType::BASIC;
  short key_note_number = 0;
  float randomization_factor = 0.0f;
  std::string name;
  int bpm = 90;

  std::vector<std::unique_ptr<Track> > instruments;
  std::vector<std::unique_ptr<Track> > tracks;
  std::vector<Section> sections;
  
  std::vector<Pattern> patterns;
  int version = 1;

  Pattern empty_pattern;
};

#endif

