#ifndef _TRACKINFO_H_
#define _TRACKINFO_H_

class TrackInfo {
public:
  TrackInfo(bool _is_active = false) : is_active(_is_active) { }

  bool isActive() const { return is_active; }
  bool isRecording() const { return is_recording; }
  bool isClipping() const { return is_clipping; }
  
 private:
  bool is_active = false;
  bool is_recording = false;
  bool is_clipping = false;
};

#endif
