#ifndef _CHANNELCONFIGURATION_H_
#define _CHANNELCONFIGURATION_H_

class ChannelConfiguration {
 public:
  enum ConfigurationType { MONO = 1, STEREO, AMBISONIC };

  ChannelConfiguration() : type_(MONO), audioOutSampleRate_(44100), dmxOutSampleRate_(0) { }
  ChannelConfiguration(ConfigurationType type, int audioOutSampleRate, int ambisonic_order = 1) : type_(type), audioOutSampleRate_(audioOutSampleRate), dmxOutSampleRate_(0), ambisonic_order_(ambisonic_order) { }
  bool operator==(const ChannelConfiguration & other) const {
    return type_ == other.type_ && audioOutSampleRate_ == other.audioOutSampleRate_ && dmxOutSampleRate_ == other.dmxOutSampleRate_ && ambisonic_order_ == other.ambisonic_order_;
  }

  int numberOfChannels() const {
    switch (type_) {
    case MONO: return 1;
    case STEREO: return 2;
    case AMBISONIC: return (ambisonic_order_ + 1) * (ambisonic_order_ + 1);
    default: return 1;
    }
  }

  // The device/WAV output always has this many channels, regardless of the
  // bus's own channel count - an ambisonic bus is always decoded down to
  // stereo (binaural or cardioid) before reaching a real output device.
  int getDeviceChannels() const { return type_ == AMBISONIC ? 2 : numberOfChannels(); }

  ConfigurationType getType() const { return type_; }
  int getAudioOutSampleRate() const { return audioOutSampleRate_; }
  int getDMXOutSampleRate() const { return dmxOutSampleRate_; }
  int getAmbisonicOrder() const { return ambisonic_order_; }

  void setType(ConfigurationType type) { type_ = type; }
  void setAudioOutSampleRate(int audioOutSampleRate) { audioOutSampleRate_ = audioOutSampleRate; }
  void setAmbisonicOrder(int ambisonic_order) { ambisonic_order_ = ambisonic_order; }

  inline float getRowDuration(int tempo) const {
    return 60.0f / 4.0f / tempo;
  }
  
  inline int getSampleInterval(int tempo) const {
    return static_cast<int>(getRowDuration(tempo) * getAudioOutSampleRate());
  }

 private:
  ConfigurationType type_;
  int audioOutSampleRate_;
  int dmxOutSampleRate_;
  int imageOutSampleRate_;
  int imageWidth = 0, imageHeight_ = 0;
  int ambisonic_order_ = 1;
};

#endif
