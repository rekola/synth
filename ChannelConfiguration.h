#ifndef _CHANNELCONFIGURATION_H_
#define _CHANNELCONFIGURATION_H_

class ChannelConfiguration {
 public:
  // ambisonic_order 0 = mono (no directional content - a single
  // omnidirectional/W channel, conceptually 0th-order ambisonics), 1 =
  // first-order/FOA (4 channels), 2 = second-order (9 channels), 3 =
  // third-order (16 channels) - see AmbisonicEncoding.h's kAmbisonicOrder
  // hard ceiling.
  explicit ChannelConfiguration(int audioOutSampleRate = 44100, int ambisonic_order = 0)
    : audioOutSampleRate_(audioOutSampleRate), ambisonic_order_(ambisonic_order) { }

  bool operator==(const ChannelConfiguration & other) const {
    return audioOutSampleRate_ == other.audioOutSampleRate_ && ambisonic_order_ == other.ambisonic_order_;
  }

  bool isMono() const { return ambisonic_order_ == 0; }
  bool isAmbisonic() const { return ambisonic_order_ > 0; }

  int numberOfChannels() const { return (ambisonic_order_ + 1) * (ambisonic_order_ + 1); }

  // Every mixer ultimately produces a 2-channel stereo device signal - a
  // MONO (0th-order-ambisonic, W-only) bus broadcasts equally to both
  // channels via decodeToStereo()'s 1-channel case rather than being a
  // genuine 1-channel device output; an AMBISONIC bus decodes via binaural
  // or cardioid. There is no other device-output shape.
  int getDeviceChannels() const { return 2; }

  int getAudioOutSampleRate() const { return audioOutSampleRate_; }
  int getAmbisonicOrder() const { return ambisonic_order_; }

  void setAudioOutSampleRate(int audioOutSampleRate) { audioOutSampleRate_ = audioOutSampleRate; }
  void setAmbisonicOrder(int ambisonic_order) { ambisonic_order_ = ambisonic_order; }

  inline float getRowDuration(int tempo) const {
    return 60.0f / 4.0f / tempo;
  }

  inline int getSampleInterval(int tempo) const {
    return static_cast<int>(getRowDuration(tempo) * getAudioOutSampleRate());
  }

 private:
  int audioOutSampleRate_;
  int ambisonic_order_;
};

#endif
