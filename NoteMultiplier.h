#ifndef _NOTEMULTIPLIER_H_
#define _NOTEMULTIPLIER_H_

#include "Track.h"
#include "SphericalPosition.h"
#include "SendLevels.h"

class NoteMultiplier : public Track {
 public:
  NoteMultiplier() : Track(TrackType::EFFECT) { }

  const char * getElementName() const override { return "multiply"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, const SendLevels & sends) const override;

private:
  int unisons_ = 1;
  int octaves_ = 0;
  int fifths_ = 0;
  int fourths_ = 0;

  float detune_ = 0;
  float spread_ = 0;
};

#endif
