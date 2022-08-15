#ifndef _FILTER_H_
#define _FILTER_H_

#include "../Track.h"
#include "../Envelope.h"

class Filter : public Track {
 public:
  Filter() : Track(TrackType::EFFECT) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;
  std::string getElementName() const override { return "filter"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  float get_fcut_min() const { return fcut_min_; }
  float get_fcut_max() const { return fcut_max_; }
  float get_fres() const { return fres_; }
  bool get_is_highpass() const { return is_highpass_; }

private:
  float fcut_min_ = 0.0f, fcut_max_ = 0.0f, fres_ = 0.0f;
  bool is_highpass_ = false, aftertouch_ = false;
  Envelope envelope_;
};

#endif
