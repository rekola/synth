#ifndef _ARPEGGIATOR_H_
#define _ARPEGGIATOR_H_

#include "Track.h"
#include "SphericalPosition.h"

class Arpeggiator : public Track {
 public:
  enum Mode {
    UP = 1,
    DOWN,
    UP_DOWN
  };
  Arpeggiator() : Track(TrackType::EFFECT) { }

  const char * getElementName() const override { return "arpeggiator"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const override;

private:
  Mode mode_ = UP;
  int note_duration_ = 1;
  int octaves_ = 1;
  int gate_ = 1; // length of the note
};

#endif
