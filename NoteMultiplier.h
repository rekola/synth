#ifndef _NOTEMULTIPLIER_H_
#define _NOTEMULTIPLIER_H_

#include "Track.h"

class NoteMultiplier : public Track {
 public:
  NoteMultiplier() : Track(TrackType::EFFECT) { }

  std::string getElementName() const override { return "multiply"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, float azimuth, float frequency, float velocity, float start_phase) const override;

private:
  int unisons = 0;
  int octaves = 0;
  int fifths = 0;
  int fourths = 0;

  float detune = 0;
  float spread = 0;
};

#endif
