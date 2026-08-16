#ifndef _TRACKINFO_H_
#define _TRACKINFO_H_

class TrackInfo {
public:
  TrackInfo(bool is_active = false, bool is_clipping = false, float meter_value = -1.0f)
    : is_active_(is_active), is_clipping_(is_clipping), meter_value_(meter_value) { }

  bool isActive() const { return is_active_; }
  bool isRecording() const { return is_recording_; }
  bool isClipping() const { return is_clipping_; }
  float getMeterValue() const { return meter_value_; }
  
 private:
  bool is_active_;
  bool is_recording_;
  bool is_clipping_;
  float meter_value_;
};

#endif
