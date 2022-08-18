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

  float get_cut_min() const { return cut_min_; }
  float get_cut_max() const { return cut_max_; }
  float get_res() const { return res_; }
  bool get_is_highpass() const { return is_highpass_; }

private:
  float cut_min_ = 0.0f, cut_max_ = 0.0f, res_ = 0.0f;
  bool is_highpass_ = false, use_aftertouch_ = false;
  Envelope envelope_;
};

#endif
