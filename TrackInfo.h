#ifndef _TRACKINFO_H_
#define _TRACKINFO_H_

class TrackInfo {
public:
  TrackInfo(bool is_active = false) : is_active_(is_active) { }

  bool isActive() const { return is_active_; }
  bool isRecording() const { return is_recording_; }
  bool isClipping() const { return is_clipping_; }
  
 private:
  bool is_active_ = false;
  bool is_recording_ = false;
  bool is_clipping_ = false;
};

#endif
