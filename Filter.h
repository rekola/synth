#ifndef _FILTER_H_
#define _FILTER_H_

#include "Effect.h"
#include "Envelope.h"

class Filter : public Effect {
 public:
  Filter() { }

  std::unique_ptr<TrackState> createState(ChannelConfiguration config, unsigned int outSampleRate) const override;
  std::string getElementName() const override { return "filter"; }
  void readXML(tinyxml2::XMLElement & element) override;
  void populateXML(tinyxml2::XMLElement & element) const override;

  float get_fcut_min() const { return fcut_min; }
  float get_fcut_max() const { return fcut_max; }
  float get_fres() const { return fres; }
  bool get_is_highpass() const { return is_highpass; }

private:
  float fcut_min = 0.0f, fcut_max = 0.0f, fres = 0.0f;
  bool is_highpass = false, aftertouch = false;
  Envelope envelope;
};

#endif
