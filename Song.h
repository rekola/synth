#ifndef _SONG_H_
#define _SONG_H_

#include "StatefulSongObject.h"
#include "Track.h"
#include "Section.h"
#include "Pattern.h"
#include "bus/DelayPattern.h"

#include <memory>
#include <vector>

class InstrumentProvider;
class Mixer;

class Song : public StatefulSongObject {
 public:
  Song(Tuning tuning = Tuning::TET12, short key = -1, float randomization_factor = 0.01f) : tuning_(tuning), key_note_number_(key), randomization_factor_(randomization_factor) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;

  Tuning getTuning() const { return tuning_; }
  void setTuning(Tuning tuning) { tuning_ = tuning; }

  short getKey() const { return key_note_number_; }
  void setKey(int key) { key_note_number_ = key; }
  
  float getRandomizationFactor() const { return randomization_factor_; }
  void setRandomizationFactor(float f) { randomization_factor_ = f; }
  
  short getTempo() const { return bpm_; }
  void setTempo(short bpm) { bpm_ = bpm; }

  // Shared spatial reverb (SendA, see SendBusProcessor/FDNReverb) - static
  // song-level settings, like every other effect parameter in this
  // codebase (no automation mechanism exists for continuously varying an
  // effect parameter from pattern data today).
  float getReverbSize() const { return reverb_size_; }
  void setReverbSize(float f) { reverb_size_ = f; }
  float getReverbDecay() const { return reverb_decay_; }
  void setReverbDecay(float f) { reverb_decay_ = f; }
  float getReverbDamping() const { return reverb_damping_; }
  void setReverbDamping(float f) { reverb_damping_ = f; }
  float getReverbPreDelay() const { return reverb_predelay_; }
  void setReverbPreDelay(float f) { reverb_predelay_ = f; }
  float getReverbWet() const { return reverb_wet_; }
  void setReverbWet(float f) { reverb_wet_ = f; }

  // Shared multi-tap delay (SendB, see SendBusProcessor/MultiTapDelay) -
  // same static, song-level-only shape as the reverb parameters above.
  // delayBaseRows: base tap interval in pattern rows (the fixed 4-tap
  // table's shortest tap; the other 3 are 2x/4x/8x this). delayPattern:
  // how the feedback tap's direction/gain evolves pass to pass - see
  // DelayPattern/MultiTapDelay.h.
  float getDelayBaseRows() const { return delay_base_rows_; }
  void setDelayBaseRows(float f) { delay_base_rows_ = f; }
  float getDelayFeedback() const { return delay_feedback_; }
  void setDelayFeedback(float f) { delay_feedback_ = f; }
  float getDelayDamping() const { return delay_damping_; }
  void setDelayDamping(float f) { delay_damping_ = f; }
  float getDelayWet() const { return delay_wet_; }
  void setDelayWet(float f) { delay_wet_ = f; }
  DelayPattern getDelayPattern() const { return delay_pattern_; }
  void setDelayPattern(DelayPattern p) { delay_pattern_ = p; }
  float getDelayPatternSpeed() const { return delay_pattern_speed_; }
  void setDelayPatternSpeed(float f) { delay_pattern_speed_ = f; }

  void incVersion() { version_++; }
  int getVersion() const { return version_; }

  const std::vector<Section> & getSections() const { return sections_; }
  const std::vector<Pattern> & getPatterns() const { return patterns_; }
  const Pattern & getPattern(int i) const { return i >= 0 && i < static_cast<int>(patterns_.size()) ? patterns_[i] : empty_pattern_; }
  Pattern & getPattern(int i) { return i >= 0 && i < static_cast<int>(patterns_.size()) ? patterns_[i] : empty_pattern_; }

  std::pair<int, int> normalizePosition(int pattern_idx, int row_idx) const {
    while (pattern_idx < static_cast<int>(patterns_.size())) {
      auto & pattern = patterns_[pattern_idx];
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
    sections_.push_back(std::move(section));
    return sections_.back();
  }

  Section & addSection() { return addSection(Section()); }

  Pattern & addPattern(Pattern pattern) {
    incVersion();
    patterns_.push_back(std::move(pattern));
    return patterns_.back();
  }

  Pattern & addPattern(int rows) { return addPattern(Pattern(rows)); }
    
  const std::vector<std::unique_ptr<Track> > & getInstruments() const { return instruments_; }
  const Track & getInstrument(int i) const { return *(instruments_[i]); }
  void addInstrument(std::unique_ptr<Track> i) {
    instruments_.push_back(std::move(i));
    incVersion();
  }

  bool open(const std::string & filename, const InstrumentProvider & provider);
  void save(const std::string & filename) const;

  std::vector<std::unique_ptr<Track> > & getTracks() { return tracks_; }
  const std::vector<std::unique_ptr<Track> > & getTracks() const { return tracks_; }

  Track & addTrack(std::unique_ptr<Track> track) {
    tracks_.push_back(std::move(track));
    incVersion();
    return *(tracks_.back());
  }

  const Track * getTrackByInternalId(int id) const {
    for (auto & track : getTracks()) {
      auto r = track->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getTrackByInternalId(int id) {
    for (auto & track : getTracks()) {
      auto r = track->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  const Track * getTrackById(std::string_view id) const {
    for (auto & track : getTracks()) {
      auto r = track->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getTrackById(std::string_view id) {
    for (auto & track : getTracks()) {
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

  // Flattens the track tree into the leaf tracks (INSTRUMENT_CONTROL/
  // PERCUSSION_CONTROL/SAMPLE) that are actually addressable as "a track" -
  // Group/effect-chain wrapper tracks are skipped over, not listed
  // themselves. The single canonical definition of "the addressable track
  // list and its order" - every caller that needs to resolve a track by
  // position (PatternEditor's columns, a Launchpad device's assigned
  // track) uses this same one, so they can never quietly diverge.
  std::vector<int> getRootTrackIds() const;

private:
  Tuning tuning_ = Tuning::TET12;
  short key_note_number_ = 0;
  float randomization_factor_ = 0.0f;
  int bpm_ = 90;
  float reverb_size_ = 1.0f;
  float reverb_decay_ = 1.8f;
  float reverb_damping_ = 0.1f;
  float reverb_predelay_ = 0.02f;
  float reverb_wet_ = 0.2512f;
  float delay_base_rows_ = 0.1875f; // 3/16
  float delay_feedback_ = 0.5f;
  float delay_damping_ = 0.5f;
  float delay_wet_ = 0.354f; // -9dB
  DelayPattern delay_pattern_ = DelayPattern::Static;
  float delay_pattern_speed_ = 18.0f;
  int version_ = 1;

  std::vector<std::unique_ptr<Track> > instruments_;
  std::vector<std::unique_ptr<Track> > tracks_;
  std::vector<Section> sections_;
  std::vector<Pattern> patterns_;
  
  static inline Pattern empty_pattern_;
};

#endif

