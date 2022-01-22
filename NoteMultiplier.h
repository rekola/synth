#ifndef _NOTEMULTIPLIER_H_
#define _NOTEMULTIPLIER_H_

#include "Effect.h"

class NoteMultiplier : public Effect {
 public:
  NoteMultiplier() { }

  std::string getElementName() const override { return "multiply"; }
  void readXML(tinyxml2::XMLElement & element) override;
  void populateXML(tinyxml2::XMLElement & element) const override;
  std::unique_ptr<TrackState> playNote(float frequency, float velocity, unsigned int outSampleRate, float start_phase) const override;

private:
  int unisons = 0;
  int octaves = 0;
  int fifths = 0;
  int fourths = 0;

  float detune = 0;
};

#endif
