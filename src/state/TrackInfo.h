#ifndef _TRACKINFO_H_
#define _TRACKINFO_H_

class TrackInfo {
public:
  TrackInfo(bool is_active = false, bool is_clipping = false, float meter_value = -1.0f,
      float live_send_main = -1.0f, float live_send_a = -1.0f, float live_send_b = -1.0f)
    : is_active_(is_active), is_clipping_(is_clipping), meter_value_(meter_value),
      live_send_main_(live_send_main), live_send_a_(live_send_a), live_send_b_(live_send_b) { }

  bool isActive() const { return is_active_; }
  bool isRecording() const { return is_recording_; }
  bool isClipping() const { return is_clipping_; }
  float getMeterValue() const { return meter_value_; }

  // The engine's own actual, currently-live Send Main/A/B linear gain for
  // this track (LeafTrackState::renderVoices()'s own getSends(), read
  // *after* that render pass's own send-ramp advance - see
  // LeafTrackState::advanceSendRamps()) - a mid-glide value while one is
  // in flight, not wherever it's ultimately headed. -1.0f (the default)
  // means "not reported" - no live engine state exists yet for this track
  // (e.g. it's never actually been rendered this session) - distinct from
  // a real, legitimately-silent 0.0f. Controller::receivePlaybackSnapshot()
  // is what actually consumes these, syncing the model's own LeafTrack
  // sends back to whatever the engine is really doing right now, so nothing
  // else that reads the model (Launchpad's own LED refresh included) ever
  // shows a value the engine hasn't actually reached yet.
  bool hasLiveSends() const { return live_send_main_ >= 0.0f; }
  float getLiveSendMain() const { return live_send_main_; }
  float getLiveSendA() const { return live_send_a_; }
  float getLiveSendB() const { return live_send_b_; }

 private:
  bool is_active_;
  bool is_recording_;
  bool is_clipping_;
  float meter_value_;
  float live_send_main_;
  float live_send_a_;
  float live_send_b_;
};

#endif
