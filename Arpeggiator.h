#ifndef _ARPEGGIATOR_H_
#define _ARPEGGIATOR_H_

#include "Track.h"

class Arpeggiator : public Track {
 public:
  enum Mode {
    UP = 1,
    DOWN,
    UP_DOWN
  };
  Arpeggiator() : Track(TrackType::EFFECT) { }

  std::string getElementName() const override { return "arpeggiator"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, float azimuth, float frequency, float velocity, float start_phase) const override;

private:
  Mode mode_ = UP;
  int note_duration_ = 1;
  int octaves_ = 1;
  int gate_ = 1; // length of the note
};

#endif
