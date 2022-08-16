#ifndef _SONG_H_
#define _SONG_H_

#include "Track.h"
#include "Pattern.h"
#include "InstrumentSet.h"
#include "MixerType.h"

#include <memory>
#include <vector>

class SongState;
class InstrumentProvider;
class Mixer;

class Song : public Track {
 public:
  Song(Tuning _tuning = Tuning::TET12, short _key = -1, float _randomization_factor = 0.01f) : Track(TrackType::MASTER), tuning(_tuning), key_note_number(_key), randomization_factor(_randomization_factor) { }

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
  
  Pattern & addPattern(const Pattern & pattern) {
    incVersion();
    patterns.push_back(pattern);
    return patterns.back();
  }

  Pattern & addPattern(int rows, Tuning tuning = Tuning::INHERIT, int key = -1) { return addPattern(Pattern(rows, tuning, key)); }
    
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

  void render(int frames, SongState & state, Mixer & mixer);

  std::string getElementName() const override { return "song"; }

  int getSampleInterval(int outSampleRate) const {
    return 60.0 / 4.0 / getTempo() * outSampleRate;
  }

private:
  Tuning tuning;
  MixerType mixer_type = MixerType::BASIC;
  short key_note_number;
  float randomization_factor;
  std::string name;
  int bpm = 90;

  std::vector<std::unique_ptr<Track> > instruments;
  std::vector<Pattern> patterns;
  Pattern empty_pattern;
  int version = 1;
};

#endif

