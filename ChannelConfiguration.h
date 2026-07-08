#ifndef _CHANNELCONFIGURATION_H_
#define _CHANNELCONFIGURATION_H_

class ChannelConfiguration {
 public:
  enum ConfigurationType { MONO = 1, STEREO, SURROUND_5_1 };

  ChannelConfiguration() : type_(MONO), audioOutSampleRate_(44100), dmxOutSampleRate_(0) { }
  ChannelConfiguration(ConfigurationType type, int audioOutSampleRate) : type_(type), audioOutSampleRate_(audioOutSampleRate), dmxOutSampleRate_(0) { }
  bool operator==(const ChannelConfiguration & other) const {
    return type_ == other.type_ && audioOutSampleRate_ == other.audioOutSampleRate_ && dmxOutSampleRate_ == other.dmxOutSampleRate_;
  }
  
  int numberOfChannels() const { return type_ == MONO ? 1 : 2; }

  ConfigurationType getType() const { return type_; }
  int getAudioOutSampleRate() const { return audioOutSampleRate_; }
  int getDMXOutSampleRate() const { return dmxOutSampleRate_; }

  void setType(ConfigurationType type) { type_ = type; }
  void setAudioOutSampleRate(int audioOutSampleRate) { audioOutSampleRate_ = audioOutSampleRate; }

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
};

#endif
