#ifndef _EFFECT_H_
#define _EFFECT_H_

#include "../model/Track.h"

class Effect : public Track {
 public:
  Effect() : Track(TrackType::EFFECT) { }

 protected:
  void setVendorName(std::string vendor_name) { vendor_name_ = std::move(vendor_name); }
  
 private:
  std::string vendor_name_;
};

#endif
