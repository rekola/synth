#ifndef _SOUNDFONTINSTRUMENT_H_
#define _SOUNDFONTINSTRUMENT_H_

#include "Instrument.h"

#include <string>

#define TSF_BOOL char

// Supported output modes by the render methods
enum TSFOutputMode {
 // Two channels with single left/right samples one after another
 TSF_STEREO_INTERLEAVED,
 // Two channels with all samples for the left channel first then right
 TSF_STEREO_UNWEAVED,
 // A single channel (stereo instruments are mixed into center)
 TSF_MONO,
};

typedef struct tsf tsf;
typedef char tsf_fourcc[4];
typedef signed char tsf_s8;
typedef unsigned char tsf_u8;
typedef unsigned short tsf_u16;
typedef signed short tsf_s16;
typedef unsigned int tsf_u32;
typedef char tsf_char20[20];

struct tsf_riffchunk { tsf_fourcc id; tsf_u32 size; };
struct tsf_envelope { float delay, attack, hold, decay, sustain, release, keynumToHold, keynumToDecay; };
struct tsf_voice_envelope { float level, slope; int samplesUntilNextSegment; short segment, midiVelocity; struct tsf_envelope parameters; TSF_BOOL segmentIsExponential, isAmpEnv; };
struct tsf_voice_lowpass { double QInv, a0, a1, b1, b2, z1, z2; TSF_BOOL active; };
struct tsf_voice_lfo { int samplesUntil; float level, delta; };

struct tsf_region
{
  int loop_mode;
  unsigned int sample_rate;
  unsigned char lokey, hikey, lovel, hivel;
  unsigned int group, offset, end, loop_start, loop_end;
  int transpose, tune, pitch_keycenter, pitch_keytrack;
  float attenuation, pan;
  struct tsf_envelope ampenv, modenv;
  int initialFilterQ, initialFilterFc;
  int modEnvToPitch, modEnvToFilterFc, modLfoToFilterFc, modLfoToVolume;
  float delayModLFO;
  int freqModLFO, modLfoToPitch;
  float delayVibLFO;
  int freqVibLFO, vibLfoToPitch;
};

struct tsf_preset
{
  tsf_char20 presetName;
  tsf_u16 preset, bank;
  struct tsf_region* regions;
  int regionNum;
};

struct tsf_voice
{
  int playingPreset, playingKey, playingChannel;
  struct tsf_region* region;
  double pitchInputTimecents, pitchOutputFactor;
  double sourceSamplePosition;
  float  noteGainDB, panFactorLeft, panFactorRight;
  unsigned int playIndex, loopStart, loopEnd;
  struct tsf_voice_envelope ampenv, modenv;
  struct tsf_voice_lowpass lowpass;
  struct tsf_voice_lfo modlfo, viblfo;
};

struct tsf_channel
{
  unsigned short presetIndex, bank, pitchWheel, midiPan, midiVolume, midiExpression, midiRPN, midiData;
  float panOffset, gainDB, pitchRange, tuning;
};

struct tsf_channels
{
  void (*setupVoice)(tsf* f, struct tsf_voice* voice);
  struct tsf_channel* channels;
  int channelNum, activeChannel;
};

struct tsf {
  struct tsf_preset* presets;
  float* fontSamples;
  struct tsf_voice* voices;
  struct tsf_channels* channels;
  float* outputSamples;
  
  int presetNum;
  int voiceNum;
  int maxVoiceNum;
  int outputSampleSize;
  unsigned int voicePlayIndex;
  
  enum TSFOutputMode outputmode;
  float outSampleRate;
  float globalGainDB;
};

class SoundFontInstrument : public Instrument {
 public:  
  explicit SoundFontInstrument(std::string _filename) : filename(_filename) {
    openFile();
  }

  float getSample(InstrumentVoice & voice) const override;
  void openFile();
  std::shared_ptr<InstrumentVoice> createVoice() const override;
  
private:
  tsf * tsf_handle = 0;
  std::string filename;
};

#endif
