#ifndef _RESONANTLOWPASS_H_
#define _RESONANTLOWPASS_H_

#include "../Track.h"
#include "../Envelope.h"

class ResonantLowpass : public Track {
 public:
  ResonantLowpass() : Track(TrackType::EFFECT) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;
  const char * getElementName() const override { return "resonantLowpass"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  float get_cut_min() const { return cut_min_; }
  float get_cut_max() const { return cut_max_; }
  float get_res() const { return res_; }

private:
  float cut_min_ = 0.0f, cut_max_ = 0.0f, res_ = 0.0f;
  bool use_aftertouch_ = false;
  Envelope envelope_;
};

#endif
