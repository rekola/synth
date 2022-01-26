#ifndef _NOTEMULTIPLIER_H_
#define _NOTEMULTIPLIER_H_

#include "Effect.h"

class NoteMultiplier : public Effect {
 public:
  NoteMultiplier() { }

  std::string getElementName() const override { return "multiply"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(ChannelConfiguration channel_config, int outSampleRate, float azimuth, float frequency, float velocity, float start_phase) const override;

private:
  int unisons = 0;
  int octaves = 0;
  int fifths = 0;
  int fourths = 0;

  float detune = 0;
  float spread = 0;
};

#endif
