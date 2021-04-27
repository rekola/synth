/* LICENSE (MIT)

   Copyright (C) 2021, Mikael Rekola
   Based on TinySoundFont, Copyright (C) 2017, 2018 Bernhard Schelling
   Based on SFZero, Copyright (C) 2012 Steve Folta (https://github.com/stevefolta/SFZero)

   Permission is hereby granted, free of charge, to any person obtaining a copy of this
   software and associated documentation files (the "Software"), to deal in the Software
   without restriction, including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
   to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "SoundFontInstrument.h"

using namespace std;

// Supported output modes by the render methods
enum TSFOutputMode {
 // Two channels with single left/right samples one after another
 TSF_STEREO_INTERLEAVED,
 // Two channels with all samples for the left channel first then right
 TSF_STEREO_UNWEAVED,
 // A single channel (stereo instruments are mixed into center)
 TSF_MONO,
};

typedef char tsf_fourcc[4];
typedef signed char tsf_s8;
typedef unsigned char tsf_u8;
typedef unsigned short tsf_u16;
typedef signed short tsf_s16;
typedef unsigned int tsf_u32;
typedef char tsf_char20[20];

struct tsf_riffchunk { tsf_fourcc id; tsf_u32 size; };
struct tsf_envelope { float delay, attack, hold, decay, sustain, release, keynumToHold, keynumToDecay; };
struct tsf_voice_envelope { float level, slope; int samplesUntilNextSegment; short segment, midiVelocity; struct tsf_envelope parameters; bool segmentIsExponential, isAmpEnv; };
struct tsf_voice_lowpass { double QInv, a0, a1, b1, b2, z1, z2; bool active; };
struct tsf_voice_lfo { int samplesUntil; float level, delta; };

struct tsf_region {
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

struct tsf_preset {
  tsf_char20 presetName;
  tsf_u16 preset, bank;
  struct tsf_region* regions;
  int regionNum;
};

struct tsf_voice {
  int playingPreset, playingChannel;
  double apparentPlayingKey;
  struct tsf_region* region;
  double pitchInputTimecents, pitchOutputFactor;
  double sourceSamplePosition;
  float  noteGainDB, panFactorLeft, panFactorRight;
  unsigned int loopStart, loopEnd;
  struct tsf_voice_envelope ampenv, modenv;
  struct tsf_voice_lowpass lowpass;
  struct tsf_voice_lfo modlfo, viblfo;
};

struct tsf_channel {
  unsigned short presetIndex, bank, pitchWheel, midiPan, midiVolume, midiExpression, midiRPN, midiData;
  float panOffset, gainDB, pitchRange, tuning;
};

struct tsf_channels {
  void (*setupVoice)(tsf* f, struct tsf_voice* voice);
  struct tsf_channel* channels;
  int channelNum, activeChannel;
};

struct tsf {
  struct tsf_preset* presets;
  float* fontSamples;
  struct tsf_channels* channels;
  float* outputSamples;
  
  int presetNum;
  int outputSampleSize;
  
  enum TSFOutputMode outputmode;
  float outSampleRate;
  float globalGainDB;
};

// Stream structure for the generic loading
struct tsf_stream {
  // Custom data given to the functions as the first parameter
  void* data;
  
  // Function pointer will be called to read 'size' bytes into ptr (returns number of read bytes)
  int (*read)(void* data, void* ptr, unsigned int size);
  
  // Function pointer will be called to skip ahead over 'count' bytes (returns 1 on success, 0 on error)
  int (*skip)(void* data, unsigned int count);
};

// Generic SoundFont loading method using the stream structure above
tsf* tsf_load(struct tsf_stream* stream);

// Returns the preset index from a bank and preset number, or -1 if it does not exist in the loaded SoundFont
int tsf_get_presetindex(const tsf* f, int bank, int preset_number);

// Returns the number of presets in the loaded SoundFont
int tsf_get_presetcount(const tsf* f);

// Returns the name of a preset index >= 0 and < tsf_get_presetcount()
const char* tsf_get_presetname(const tsf* f, int preset_index);

// Returns the name of a preset by bank and preset number
const char* tsf_bank_get_presetname(const tsf* f, int bank, int preset_number);

// Thread safety:
// Your audio output which calls the tsf_render* functions will most likely
// run on a different thread than where the playback tsf_note* functions
// are called. In which case some sort of concurrency control like a
// mutex needs to be used so they are not called at the same time.
// Alternatively, you can pre-allocate a maximum number of voices that can
// play simultaneously by calling tsf_set_max_voices after loading.
// That way memory re-allocation will not happen during tsf_note_on and
// TSF should become mostly thread safe.
// There is a theoretical chance that ending notes would negatively influence
// a voice that is rendering at the time but it is hard to say.
// Also be aware, this has not been tested much.

// Setup the parameters for the voice render methods
//   outputmode: if mono or stereo and how stereo channel data is ordered
//   samplerate: the number of samples per second (output frequency)
//   global_gain_db: volume gain in decibels (>0 means higher, <0 means lower)

// Higher level channel based functions, set up channel parameters
//   channel: channel number
//   preset_index: preset index >= 0 and < tsf_get_presetcount()
//   preset_number: preset number (alternative to preset_index)
//   flag_mididrums: 0 for normal channels, otherwise apply MIDI drum channel rules
//   bank: instrument bank number (alternative to preset_index)
//   pan: stereo panning value from 0.0 (left) to 1.0 (right) (default 0.5 center)
//   volume: linear volume scale factor (default 1.0 full)
//   pitch_wheel: pitch wheel position 0 to 16383 (default 8192 unpitched)
//   pitch_range: range of the pitch wheel in semitones (default 2.0, total +/- 2 semitones)
//   tuning: tuning of all playing voices in semitones (default 0.0, standard (A440) tuning)
//   (set_preset_number and set_bank_preset return 0 if preset does not exist, otherwise 1)
void tsf_channel_set_presetindex(tsf* f, int channel, int preset_index);
int  tsf_channel_set_presetnumber(tsf* f, int channel, int preset_number, int flag_mididrums);
void tsf_channel_set_bank(tsf* f, int channel, int bank);
int  tsf_channel_set_bank_preset(tsf* f, int channel, int bank, int preset_number);
void tsf_channel_set_pan(tsf* f, int channel, float pan);
void tsf_channel_set_volume(tsf* f, int channel, float volume);
void tsf_channel_set_pitchwheel(tsf* f, int channel, int pitch_wheel);
void tsf_channel_set_pitchrange(tsf* f, int channel, float pitch_range);
void tsf_channel_set_tuning(tsf* f, int channel, float tuning);

// The lower this block size is the more accurate the effects are.
// Increasing the value significantly lowers the CPU usage of the voice rendering.
// If LFO affects the low-pass filter it can be hearable even as low as 8.
#ifndef TSF_RENDER_EFFECTSAMPLEBLOCK
#define TSF_RENDER_EFFECTSAMPLEBLOCK 64
#endif

// Grace release time for quick voice off (avoid clicking noise)
#define TSF_FASTRELEASETIME 0.01f

#include <cstring>
#include <cmath>
#include <cstdio>

#define TSF_FourCCEquals(value1, value2) (value1[0] == value2[0] && value1[1] == value2[1] && value1[2] == value2[2] && value1[3] == value2[3])

static int tsf_stream_stdio_read(FILE* f, void* ptr, unsigned int size) { return (int)fread(ptr, 1, size, f); }
static int tsf_stream_stdio_skip(FILE* f, unsigned int count) { return !fseek(f, count, SEEK_CUR); }

// Directly load a SoundFont from a .sf2 file path

tsf* tsf_load_filename(const char* filename) {
  tsf* res;
  struct tsf_stream stream = { NULL, (int(*)(void*,void*,unsigned int))&tsf_stream_stdio_read, (int(*)(void*,unsigned int))&tsf_stream_stdio_skip };
#if __STDC_WANT_SECURE_LIB__
  FILE* f = NULL; fopen_s(&f, filename, "rb");
#else
  FILE* f = fopen(filename, "rb");
#endif
  if (!f)
    {
      //if (e) *e = TSF_FILENOTFOUND;
      return NULL;
    }
  stream.data = f;
  res = tsf_load(&stream);
  fclose(f);
  return res;
}

struct tsf_stream_memory { const char* buffer; unsigned int total, pos; };
static int tsf_stream_memory_read(struct tsf_stream_memory* m, void* ptr, unsigned int size) { if (size > m->total - m->pos) size = m->total - m->pos; memcpy(ptr, m->buffer+m->pos, size); m->pos += size; return size; }
static int tsf_stream_memory_skip(struct tsf_stream_memory* m, unsigned int count) { if (m->pos + count > m->total) return 0; m->pos += count; return 1; }

// Load a SoundFont from a block of memory

tsf* tsf_load_memory(const void* buffer, int size) {
  struct tsf_stream stream = { NULL, (int(*)(void*,void*,unsigned int))&tsf_stream_memory_read, (int(*)(void*,unsigned int))&tsf_stream_memory_skip };
  struct tsf_stream_memory f = { 0, 0, 0 };
  f.buffer = (const char*)buffer;
  f.total = size;
  stream.data = &f;
  return tsf_load(&stream);
}

enum { TSF_LOOPMODE_NONE, TSF_LOOPMODE_CONTINUOUS, TSF_LOOPMODE_SUSTAIN };

enum { TSF_SEGMENT_NONE, TSF_SEGMENT_DELAY, TSF_SEGMENT_ATTACK, TSF_SEGMENT_HOLD, TSF_SEGMENT_DECAY, TSF_SEGMENT_SUSTAIN, TSF_SEGMENT_RELEASE, TSF_SEGMENT_DONE };

struct tsf_hydra
{
	struct tsf_hydra_phdr *phdrs; struct tsf_hydra_pbag *pbags; struct tsf_hydra_pmod *pmods;
	struct tsf_hydra_pgen *pgens; struct tsf_hydra_inst *insts; struct tsf_hydra_ibag *ibags;
	struct tsf_hydra_imod *imods; struct tsf_hydra_igen *igens; struct tsf_hydra_shdr *shdrs;
	int phdrNum, pbagNum, pmodNum, pgenNum, instNum, ibagNum, imodNum, igenNum, shdrNum;
};

union tsf_hydra_genamount { struct { tsf_u8 lo, hi; } range; tsf_s16 shortAmount; tsf_u16 wordAmount; };
struct tsf_hydra_phdr { tsf_char20 presetName; tsf_u16 preset, bank, presetBagNdx; tsf_u32 library, genre, morphology; };
struct tsf_hydra_pbag { tsf_u16 genNdx, modNdx; };
struct tsf_hydra_pmod { tsf_u16 modSrcOper, modDestOper; tsf_s16 modAmount; tsf_u16 modAmtSrcOper, modTransOper; };
struct tsf_hydra_pgen { tsf_u16 genOper; union tsf_hydra_genamount genAmount; };
struct tsf_hydra_inst { tsf_char20 instName; tsf_u16 instBagNdx; };
struct tsf_hydra_ibag { tsf_u16 instGenNdx, instModNdx; };
struct tsf_hydra_imod { tsf_u16 modSrcOper, modDestOper; tsf_s16 modAmount; tsf_u16 modAmtSrcOper, modTransOper; };
struct tsf_hydra_igen { tsf_u16 genOper; union tsf_hydra_genamount genAmount; };
struct tsf_hydra_shdr { tsf_char20 sampleName; tsf_u32 start, end, startLoop, endLoop, sampleRate; tsf_u8 originalPitch; tsf_s8 pitchCorrection; tsf_u16 sampleLink, sampleType; };

#define TSFR(FIELD) stream->read(stream->data, &i->FIELD, sizeof(i->FIELD));
static void tsf_hydra_read_phdr(struct tsf_hydra_phdr* i, struct tsf_stream* stream) { TSFR(presetName) TSFR(preset) TSFR(bank) TSFR(presetBagNdx) TSFR(library) TSFR(genre) TSFR(morphology) }
static void tsf_hydra_read_pbag(struct tsf_hydra_pbag* i, struct tsf_stream* stream) { TSFR(genNdx) TSFR(modNdx) }
static void tsf_hydra_read_pmod(struct tsf_hydra_pmod* i, struct tsf_stream* stream) { TSFR(modSrcOper) TSFR(modDestOper) TSFR(modAmount) TSFR(modAmtSrcOper) TSFR(modTransOper) }
static void tsf_hydra_read_pgen(struct tsf_hydra_pgen* i, struct tsf_stream* stream) { TSFR(genOper) TSFR(genAmount) }
static void tsf_hydra_read_inst(struct tsf_hydra_inst* i, struct tsf_stream* stream) { TSFR(instName) TSFR(instBagNdx) }
static void tsf_hydra_read_ibag(struct tsf_hydra_ibag* i, struct tsf_stream* stream) { TSFR(instGenNdx) TSFR(instModNdx) }
static void tsf_hydra_read_imod(struct tsf_hydra_imod* i, struct tsf_stream* stream) { TSFR(modSrcOper) TSFR(modDestOper) TSFR(modAmount) TSFR(modAmtSrcOper) TSFR(modTransOper) }
static void tsf_hydra_read_igen(struct tsf_hydra_igen* i, struct tsf_stream* stream) { TSFR(genOper) TSFR(genAmount) }
static void tsf_hydra_read_shdr(struct tsf_hydra_shdr* i, struct tsf_stream* stream) { TSFR(sampleName) TSFR(start) TSFR(end) TSFR(startLoop) TSFR(endLoop) TSFR(sampleRate) TSFR(originalPitch) TSFR(pitchCorrection) TSFR(sampleLink) TSFR(sampleType) }
#undef TSFR

static double tsf_timecents2Secsd(double timecents) { return pow(2.0, timecents / 1200.0); }
static float tsf_timecents2Secsf(float timecents) { return powf(2.0f, timecents / 1200.0f); }
static float tsf_cents2Hertz(float cents) { return 8.176f * powf(2.0f, cents / 1200.0f); }
static float tsf_decibelsToGain(float db) { return (db > -100.f ? powf(10.0f, db * 0.05f) : 0); }
static float tsf_gainToDecibels(float gain) { return (gain <= .00001f ? -100.f : (float)(20.0 * log10(gain))); }

static bool tsf_riffchunk_read(struct tsf_riffchunk* parent, struct tsf_riffchunk* chunk, struct tsf_stream* stream)
{
	bool IsRiff, IsList;
	if (parent && sizeof(tsf_fourcc) + sizeof(tsf_u32) > parent->size) return false;
	if (!stream->read(stream->data, &chunk->id, sizeof(tsf_fourcc)) || *chunk->id <= ' ' || *chunk->id >= 'z') return false;
	if (!stream->read(stream->data, &chunk->size, sizeof(tsf_u32))) return false;
	if (parent && sizeof(tsf_fourcc) + sizeof(tsf_u32) + chunk->size > parent->size) return false;
	if (parent) parent->size -= sizeof(tsf_fourcc) + sizeof(tsf_u32) + chunk->size;
	IsRiff = TSF_FourCCEquals(chunk->id, "RIFF"), IsList = TSF_FourCCEquals(chunk->id, "LIST");
	if (IsRiff && parent) return false; //not allowed
	if (!IsRiff && !IsList) return true; //custom type without sub type
	if (!stream->read(stream->data, &chunk->id, sizeof(tsf_fourcc)) || *chunk->id <= ' ' || *chunk->id >= 'z') return false;
	chunk->size -= sizeof(tsf_fourcc);
	return true;
}

static void tsf_region_clear(struct tsf_region* i, bool for_relative) {
  memset(i, 0, sizeof(struct tsf_region));
  i->hikey = i->hivel = 127;
  i->pitch_keycenter = 60; // C4
  if (for_relative) return;
  
  i->pitch_keytrack = 100;
  
  i->pitch_keycenter = -1;
  
  // SF2 defaults in timecents.
  i->ampenv.delay = i->ampenv.attack = i->ampenv.hold = i->ampenv.decay = i->ampenv.release = -12000.0f;
  i->modenv.delay = i->modenv.attack = i->modenv.hold = i->modenv.decay = i->modenv.release = -12000.0f;
  
  i->initialFilterFc = 13500;
  
  i->delayModLFO = -12000.0f;
  i->delayVibLFO = -12000.0f;
}

static void tsf_region_operator(struct tsf_region* region, tsf_u16 genOper, union tsf_hydra_genamount* amount, struct tsf_region* merge_region) {
  enum {
	_GEN_TYPE_MASK       = 0x0F,
	GEN_FLOAT            = 0x01,
	GEN_INT              = 0x02,
	GEN_UINT_ADD         = 0x03,
	GEN_UINT_ADD15       = 0x04,
	GEN_KEYRANGE         = 0x05,
	GEN_VELRANGE         = 0x06,
	GEN_LOOPMODE         = 0x07,
	GEN_GROUP            = 0x08,
	GEN_KEYCENTER        = 0x09,
	
	_GEN_LIMIT_MASK      = 0xF0,
	GEN_INT_LIMIT12K     = 0x10, //min -12000, max 12000
	GEN_INT_LIMITFC      = 0x20, //min 1500, max 13500
	GEN_INT_LIMITQ       = 0x30, //min 0, max 960
	GEN_INT_LIMIT960     = 0x40, //min -960, max 960
	GEN_INT_LIMIT16K4500 = 0x50, //min -16000, max 4500
	GEN_FLOAT_LIMIT12K5K = 0x60, //min -12000, max 5000
	GEN_FLOAT_LIMIT12K8K = 0x70, //min -12000, max 8000
	GEN_FLOAT_LIMIT1200  = 0x80, //min -1200, max 1200
	GEN_FLOAT_LIMITPAN   = 0x90, //* .001f, min -.5f, max .5f,
	GEN_FLOAT_LIMITATTN  = 0xA0, //* .1f, min 0, max 144.0
	GEN_FLOAT_MAX1000    = 0xB0, //min 0, max 1000
	GEN_FLOAT_MAX1440    = 0xC0, //min 0, max 1440
	
	_GEN_MAX = 59,
  };
  
#define _TSFREGIONOFFSET(TYPE, FIELD) (unsigned char)(((TYPE*)&((struct tsf_region*)0)->FIELD) - (TYPE*)0)
#define _TSFREGIONENVOFFSET(TYPE, ENV, FIELD) (unsigned char)(((TYPE*)&((&(((struct tsf_region*)0)->ENV))->FIELD)) - (TYPE*)0)
  
  static const struct { unsigned char mode, offset; } genMetas[_GEN_MAX] =
	{
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, offset               ) }, // 0 StartAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, end                  ) }, // 1 EndAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, loop_start           ) }, // 2 StartloopAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, loop_end             ) }, // 3 EndloopAddrsOffset
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, offset               ) }, // 4 StartAddrsCoarseOffset
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modLfoToPitch        ) }, // 5 ModLfoToPitch
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, vibLfoToPitch        ) }, // 6 VibLfoToPitch
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modEnvToPitch        ) }, // 7 ModEnvToPitch
		{ GEN_INT   | GEN_INT_LIMITFC      , _TSFREGIONOFFSET(         int, initialFilterFc      ) }, // 8 InitialFilterFc
		{ GEN_INT   | GEN_INT_LIMITQ       , _TSFREGIONOFFSET(         int, initialFilterQ       ) }, // 9 InitialFilterQ
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modLfoToFilterFc     ) }, //10 ModLfoToFilterFc
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modEnvToFilterFc     ) }, //11 ModEnvToFilterFc
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, end                  ) }, //12 EndAddrsCoarseOffset
		{ GEN_INT   | GEN_INT_LIMIT960     , _TSFREGIONOFFSET(         int, modLfoToVolume       ) }, //13 ModLfoToVolume
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //15 ChorusEffectsSend (unsupported)
		{ 0                                , (0                                                  ) }, //16 ReverbEffectsSend (unsupported)
		{ GEN_FLOAT | GEN_FLOAT_LIMITPAN   , _TSFREGIONOFFSET(       float, pan                  ) }, //17 Pan
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //   Unused
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONOFFSET(       float, delayModLFO          ) }, //21 DelayModLFO
		{ GEN_INT   | GEN_INT_LIMIT16K4500 , _TSFREGIONOFFSET(         int, freqModLFO           ) }, //22 FreqModLFO
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONOFFSET(       float, delayVibLFO          ) }, //23 DelayVibLFO
		{ GEN_INT   | GEN_INT_LIMIT16K4500 , _TSFREGIONOFFSET(         int, freqVibLFO           ) }, //24 FreqVibLFO
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, modenv, delay        ) }, //25 DelayModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, attack       ) }, //26 AttackModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, modenv, hold         ) }, //27 HoldModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, decay        ) }, //28 DecayModEnv
		{ GEN_FLOAT | GEN_FLOAT_MAX1000    , _TSFREGIONENVOFFSET(    float, modenv, sustain      ) }, //29 SustainModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, release      ) }, //30 ReleaseModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, modenv, keynumToHold ) }, //31 KeynumToModEnvHold
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, modenv, keynumToDecay) }, //32 KeynumToModEnvDecay
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, ampenv, delay        ) }, //33 DelayVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, attack       ) }, //34 AttackVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, ampenv, hold         ) }, //35 HoldVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, decay        ) }, //36 DecayVolEnv
		{ GEN_FLOAT | GEN_FLOAT_MAX1440    , _TSFREGIONENVOFFSET(    float, ampenv, sustain      ) }, //37 SustainVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, release      ) }, //38 ReleaseVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, ampenv, keynumToHold ) }, //39 KeynumToVolEnvHold
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, ampenv, keynumToDecay) }, //40 KeynumToVolEnvDecay
		{ 0                                , (0                                                  ) }, //   Instrument (special)
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_KEYRANGE                     , (0                                                  ) }, //43 KeyRange
		{ GEN_VELRANGE                     , (0                                                  ) }, //44 VelRange
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, loop_start           ) }, //45 StartloopAddrsCoarseOffset
		{ 0                                , (0                                                  ) }, //46 Keynum (special)
		{ 0                                , (0                                                  ) }, //47 Velocity (special)
		{ GEN_FLOAT | GEN_FLOAT_LIMITATTN  , _TSFREGIONOFFSET(       float, attenuation          ) }, //48 InitialAttenuation
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, loop_end             ) }, //50 EndloopAddrsCoarseOffset
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, transpose            ) }, //51 CoarseTune
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, tune                 ) }, //52 FineTune
		{ 0                                , (0                                                  ) }, //   SampleID (special)
		{ GEN_LOOPMODE                     , _TSFREGIONOFFSET(         int, loop_mode            ) }, //54 SampleModes
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, pitch_keytrack       ) }, //56 ScaleTuning
		{ GEN_GROUP                        , _TSFREGIONOFFSET(unsigned int, group                ) }, //57 ExclusiveClass
		{ GEN_KEYCENTER                    , _TSFREGIONOFFSET(         int, pitch_keycenter      ) }, //58 OverridingRootKey
	};

#undef _TSFREGIONOFFSET
#undef _TSFREGIONENVOFFSET

  if (amount)
    {
      int offset;
      if (genOper >= _GEN_MAX) return;
      offset = genMetas[genOper].offset;
      switch (genMetas[genOper].mode & _GEN_TYPE_MASK)
	{
	case GEN_FLOAT:      ((       float*)region)[offset]  = amount->shortAmount;     return;
	case GEN_INT:        ((         int*)region)[offset]  = amount->shortAmount;     return;
	case GEN_UINT_ADD:   ((unsigned int*)region)[offset] += amount->shortAmount;     return;
	case GEN_UINT_ADD15: ((unsigned int*)region)[offset] += amount->shortAmount<<15; return;
	case GEN_KEYRANGE:   region->lokey = amount->range.lo; region->hikey = amount->range.hi; return;
	case GEN_VELRANGE:   region->lovel = amount->range.lo; region->hivel = amount->range.hi; return;
	case GEN_LOOPMODE:   region->loop_mode       = ((amount->wordAmount&3) == 3 ? TSF_LOOPMODE_SUSTAIN : ((amount->wordAmount&3) == 1 ? TSF_LOOPMODE_CONTINUOUS : TSF_LOOPMODE_NONE)); return;
	case GEN_GROUP:      region->group           = amount->wordAmount;  return;
	case GEN_KEYCENTER:  region->pitch_keycenter = amount->shortAmount; return;
	}
    }
  else //merge regions and clamp values
    {
      for (genOper = 0; genOper != _GEN_MAX; genOper++)
	{
	  int offset = genMetas[genOper].offset;
	  switch (genMetas[genOper].mode & _GEN_TYPE_MASK)
	    {
	    case GEN_FLOAT:
	      {
		float *val = &((float*)region)[offset], vfactor, vmin, vmax;
		*val += ((float*)merge_region)[offset];
		switch (genMetas[genOper].mode & _GEN_LIMIT_MASK)
		  {
		  case GEN_FLOAT_LIMIT12K5K: vfactor =   1.0f; vmin = -12000.0f; vmax = 5000.0f; break;
		  case GEN_FLOAT_LIMIT12K8K: vfactor =   1.0f; vmin = -12000.0f; vmax = 8000.0f; break;
		  case GEN_FLOAT_LIMIT1200:  vfactor =   1.0f; vmin =  -1200.0f; vmax = 1200.0f; break;
		  case GEN_FLOAT_LIMITPAN:   vfactor = 0.001f; vmin =     -0.5f; vmax =    0.5f; break;
		  case GEN_FLOAT_LIMITATTN:  vfactor =   0.1f; vmin =      0.0f; vmax =  144.0f; break;
		  case GEN_FLOAT_MAX1000:    vfactor =   1.0f; vmin =      0.0f; vmax = 1000.0f; break;
		  case GEN_FLOAT_MAX1440:    vfactor =   1.0f; vmin =      0.0f; vmax = 1440.0f; break;
		  default: continue;
		  }
		*val *= vfactor;
		if      (*val < vmin) *val = vmin;
		else if (*val > vmax) *val = vmax;
		continue;
	      }
	    case GEN_INT:
	      {
		int *val = &((int*)region)[offset], vmin, vmax;
		*val += ((int*)merge_region)[offset];
		switch (genMetas[genOper].mode & _GEN_LIMIT_MASK)
		  {
		  case GEN_INT_LIMIT12K:     vmin = -12000; vmax = 12000; break;
		  case GEN_INT_LIMITFC:      vmin =   1500; vmax = 13500; break;
		  case GEN_INT_LIMITQ:       vmin =      0; vmax =   960; break;
		  case GEN_INT_LIMIT960:     vmin =   -960; vmax =   960; break;
		  case GEN_INT_LIMIT16K4500: vmin = -16000; vmax =  4500; break;
		  default: continue;
		  }
		if      (*val < vmin) *val = vmin;
		else if (*val > vmax) *val = vmax;
		continue;
	      }
	    case GEN_UINT_ADD:
	      {
		((unsigned int*)region)[offset] += ((unsigned int*)merge_region)[offset];
		continue;
	      }
	    }
	}
    }
}

static void tsf_region_envtosecs(struct tsf_envelope* p, bool sustainIsGain) {
  // EG times need to be converted from timecents to seconds.
  // Pin very short EG segments.  Timecents don't get to zero, and our EG is
  // happier with zero values.
  p->delay   = (p->delay   < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->delay));
  p->attack  = (p->attack  < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->attack));
  p->release = (p->release < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->release));
  
  // If we have dynamic hold or decay times depending on key number we need
  // to keep the values in timecents so we can calculate it during startNote
  if (!p->keynumToHold)  p->hold  = (p->hold  < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->hold));
  if (!p->keynumToDecay) p->decay = (p->decay < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->decay));
  
  if (p->sustain < 0.0f) p->sustain = 0.0f;
  else if (sustainIsGain) p->sustain = tsf_decibelsToGain(-p->sustain / 10.0f);
  else p->sustain = 1.0f - (p->sustain / 1000.0f);
}

static void tsf_load_presets(tsf* res, struct tsf_hydra *hydra, unsigned int fontSampleCount) {
  enum { GenInstrument = 41, GenKeyRange = 43, GenVelRange = 44, GenSampleID = 53 };
  // Read each preset.
  struct tsf_hydra_phdr *pphdr, *pphdrMax;
  for (pphdr = hydra->phdrs, pphdrMax = pphdr + hydra->phdrNum - 1; pphdr != pphdrMax; pphdr++)
    {
      int sortedIndex = 0, region_index = 0;
      struct tsf_hydra_phdr *otherphdr;
      struct tsf_preset* preset;
      struct tsf_hydra_pbag *ppbag, *ppbagEnd;
      struct tsf_region globalRegion;
      for (otherphdr = hydra->phdrs; otherphdr != pphdrMax; otherphdr++)
	{
	  if (otherphdr == pphdr || otherphdr->bank > pphdr->bank) continue;
	  else if (otherphdr->bank < pphdr->bank) sortedIndex++;
	  else if (otherphdr->preset > pphdr->preset) continue;
	  else if (otherphdr->preset < pphdr->preset) sortedIndex++;
	  else if (otherphdr < pphdr) sortedIndex++;
	}
      
      preset = &res->presets[sortedIndex];
      memcpy(preset->presetName, pphdr->presetName, sizeof(preset->presetName));
      preset->presetName[sizeof(preset->presetName)-1] = '\0'; //should be zero terminated in source file but make sure
      preset->bank = pphdr->bank;
      preset->preset = pphdr->preset;
      preset->regionNum = 0;
      
      //count regions covered by this preset
      for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++)
	{
	  unsigned char plokey = 0, phikey = 127, plovel = 0, phivel = 127;
	  struct tsf_hydra_pgen *ppgen, *ppgenEnd; struct tsf_hydra_inst *pinst; struct tsf_hydra_ibag *pibag, *pibagEnd; struct tsf_hydra_igen *pigen, *pigenEnd;
	  for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++)
	    {
	      if (ppgen->genOper == GenKeyRange) { plokey = ppgen->genAmount.range.lo; phikey = ppgen->genAmount.range.hi; continue; }
	      if (ppgen->genOper == GenVelRange) { plovel = ppgen->genAmount.range.lo; phivel = ppgen->genAmount.range.hi; continue; }
	      if (ppgen->genOper != GenInstrument) continue;
	      if (ppgen->genAmount.wordAmount >= hydra->instNum) continue;
	      pinst = hydra->insts + ppgen->genAmount.wordAmount;
	      for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++)
		{
		  unsigned char ilokey = 0, ihikey = 127, ilovel = 0, ihivel = 127;
		  for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++)
		    {
		      if (pigen->genOper == GenKeyRange) { ilokey = pigen->genAmount.range.lo; ihikey = pigen->genAmount.range.hi; continue; }
		      if (pigen->genOper == GenVelRange) { ilovel = pigen->genAmount.range.lo; ihivel = pigen->genAmount.range.hi; continue; }
		      if (pigen->genOper == GenSampleID && ihikey >= plokey && ilokey <= phikey && ihivel >= plovel && ilovel <= phivel) preset->regionNum++;
		    }
		}
	    }
	}
      
      preset->regions = (struct tsf_region*)malloc(preset->regionNum * sizeof(struct tsf_region));
      tsf_region_clear(&globalRegion, true);
      
      // Zones.
      for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++)
	{
	  struct tsf_hydra_pgen *ppgen, *ppgenEnd; struct tsf_hydra_inst *pinst; struct tsf_hydra_ibag *pibag, *pibagEnd; struct tsf_hydra_igen *pigen, *pigenEnd;
	  struct tsf_region presetRegion = globalRegion;
	  int hadGenInstrument = 0;
	  
	  // Generators.
	  for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++)
	    {
	      // Instrument.
	      if (ppgen->genOper == GenInstrument)
		{
		  struct tsf_region instRegion;
		  tsf_u16 whichInst = ppgen->genAmount.wordAmount;
		  if (whichInst >= hydra->instNum) continue;
		  
		  tsf_region_clear(&instRegion, false);
		  pinst = &hydra->insts[whichInst];
		  for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++)
		    {
		      // Generators.
		      struct tsf_region zoneRegion = instRegion;
		      int hadSampleID = 0;
		      for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++)
			{
			  if (pigen->genOper == GenSampleID)
			    {
			      struct tsf_hydra_shdr* pshdr;
			      
			      // preset region key and vel ranges are a filter for the zone regions
			      if (zoneRegion.hikey < presetRegion.lokey || zoneRegion.lokey > presetRegion.hikey) continue;
			      if (zoneRegion.hivel < presetRegion.lovel || zoneRegion.lovel > presetRegion.hivel) continue;
			      if (presetRegion.lokey > zoneRegion.lokey) zoneRegion.lokey = presetRegion.lokey;
			      if (presetRegion.hikey < zoneRegion.hikey) zoneRegion.hikey = presetRegion.hikey;
			      if (presetRegion.lovel > zoneRegion.lovel) zoneRegion.lovel = presetRegion.lovel;
			      if (presetRegion.hivel < zoneRegion.hivel) zoneRegion.hivel = presetRegion.hivel;
			      
			      // sum regions
			      tsf_region_operator(&zoneRegion, 0, NULL, &presetRegion);
			      
			      // EG times need to be converted from timecents to seconds.
			      tsf_region_envtosecs(&zoneRegion.ampenv, true);
			      tsf_region_envtosecs(&zoneRegion.modenv, false);
			      
			      // LFO times need to be converted from timecents to seconds.
			      zoneRegion.delayModLFO = (zoneRegion.delayModLFO < -11950.0f ? 0.0f : tsf_timecents2Secsf(zoneRegion.delayModLFO));
			      zoneRegion.delayVibLFO = (zoneRegion.delayVibLFO < -11950.0f ? 0.0f : tsf_timecents2Secsf(zoneRegion.delayVibLFO));
			      
			      // Fixup sample positions
			      pshdr = &hydra->shdrs[pigen->genAmount.wordAmount];
			      zoneRegion.offset += pshdr->start;
			      zoneRegion.end += pshdr->end;
			      zoneRegion.loop_start += pshdr->startLoop;
			      zoneRegion.loop_end += pshdr->endLoop;
			      if (pshdr->endLoop > 0) zoneRegion.loop_end -= 1;
			      if (zoneRegion.pitch_keycenter == -1) zoneRegion.pitch_keycenter = pshdr->originalPitch;
			      zoneRegion.tune += pshdr->pitchCorrection;
			      zoneRegion.sample_rate = pshdr->sampleRate;
			      if (zoneRegion.end && zoneRegion.end < fontSampleCount) zoneRegion.end++;
			      else zoneRegion.end = fontSampleCount;
			      
			      preset->regions[region_index] = zoneRegion;
			      region_index++;
			      hadSampleID = 1;
			    }
			  else tsf_region_operator(&zoneRegion, pigen->genOper, &pigen->genAmount, NULL);
			}
		      
		      // Handle instrument's global zone.
		      if (pibag == hydra->ibags + pinst->instBagNdx && !hadSampleID)
			instRegion = zoneRegion;
		      
		      // Modulators (TODO)
		      //if (ibag->instModNdx < ibag[1].instModNdx) addUnsupportedOpcode("any modulator");
		    }
		  hadGenInstrument = 1;
		}
	      else tsf_region_operator(&presetRegion, ppgen->genOper, &ppgen->genAmount, NULL);
	    }
	  
	  // Modulators (TODO)
	  //if (pbag->modNdx < pbag[1].modNdx) addUnsupportedOpcode("any modulator");
	  
	  // Handle preset's global zone.
	  if (ppbag == hydra->pbags + pphdr->presetBagNdx && !hadGenInstrument)
	    globalRegion = presetRegion;
	}
    }
}

static void tsf_load_samples(float** fontSamples, unsigned int* fontSampleCount, struct tsf_riffchunk *chunkSmpl, struct tsf_stream* stream)
{
	// Read sample data into float format buffer.
	float* out; unsigned int samplesLeft, samplesToRead, samplesToConvert;
	samplesLeft = *fontSampleCount = chunkSmpl->size / sizeof(short);
	out = *fontSamples = (float*)malloc(samplesLeft * sizeof(float));
	for (; samplesLeft; samplesLeft -= samplesToRead)
	{
		short sampleBuffer[1024], *in = sampleBuffer;;
		samplesToRead = (samplesLeft > 1024 ? 1024 : samplesLeft);
		stream->read(stream->data, sampleBuffer, samplesToRead * sizeof(short));

		// Convert from signed 16-bit to float.
		for (samplesToConvert = samplesToRead; samplesToConvert > 0; --samplesToConvert)
			// If we ever need to compile for big-endian platforms, we'll need to byte-swap here.
			*out++ = (float)(*in++ / 32767.0);
	}
}

static void tsf_voice_envelope_nextsegment(struct tsf_voice_envelope* e, short active_segment, float outSampleRate)
{
	switch (active_segment)
	{
		case TSF_SEGMENT_NONE:
			e->samplesUntilNextSegment = (int)(e->parameters.delay * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_DELAY;
				e->segmentIsExponential = false;
				e->level = 0.0;
				e->slope = 0.0;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_DELAY:
			e->samplesUntilNextSegment = (int)(e->parameters.attack * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				if (!e->isAmpEnv)
				{
					//mod env attack duration scales with velocity (velocity of 1 is full duration, max velocity is 0.125 times duration)
					e->samplesUntilNextSegment = (int)(e->parameters.attack * ((145 - e->midiVelocity) / 144.0f) * outSampleRate);
				}
				e->segment = TSF_SEGMENT_ATTACK;
				e->segmentIsExponential = false;
				e->level = 0.0f;
				e->slope = 1.0f / e->samplesUntilNextSegment;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_ATTACK:
			e->samplesUntilNextSegment = (int)(e->parameters.hold * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_HOLD;
				e->segmentIsExponential = false;
				e->level = 1.0f;
				e->slope = 0.0f;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_HOLD:
			e->samplesUntilNextSegment = (int)(e->parameters.decay * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_DECAY;
				e->level = 1.0f;
				if (e->isAmpEnv)
				{
					// I don't truly understand this; just following what LinuxSampler does.
					float mysterySlope = -9.226f / e->samplesUntilNextSegment;
					e->slope = expf(mysterySlope);
					e->segmentIsExponential = true;
					if (e->parameters.sustain > 0.0f)
					{
						// Again, this is following LinuxSampler's example, which is similar to
						// SF2-style decay, where "decay" specifies the time it would take to
						// get to zero, not to the sustain level.  The SFZ spec is not that
						// specific about what "decay" means, so perhaps it's really supposed
						// to specify the time to reach the sustain level.
						e->samplesUntilNextSegment = (int)(log(e->parameters.sustain) / mysterySlope);
					}
				}
				else
				{
					e->slope = -1.0f / e->samplesUntilNextSegment;
					e->samplesUntilNextSegment = (int)(e->parameters.decay * (1.0f - e->parameters.sustain) * outSampleRate);
					e->segmentIsExponential = false;
				}
				return;
			}
			/* fall through */
		case TSF_SEGMENT_DECAY:
			e->segment = TSF_SEGMENT_SUSTAIN;
			e->level = e->parameters.sustain;
			e->slope = 0.0f;
			e->samplesUntilNextSegment = 0x7FFFFFFF;
			e->segmentIsExponential = false;
			return;
		case TSF_SEGMENT_SUSTAIN:
			e->segment = TSF_SEGMENT_RELEASE;
			e->samplesUntilNextSegment = (int)((e->parameters.release <= 0 ? TSF_FASTRELEASETIME : e->parameters.release) * outSampleRate);
			if (e->isAmpEnv)
			{
				// I don't truly understand this; just following what LinuxSampler does.
				float mysterySlope = -9.226f / e->samplesUntilNextSegment;
				e->slope = expf(mysterySlope);
				e->segmentIsExponential = true;
			}
			else
			{
				e->slope = -e->level / e->samplesUntilNextSegment;
				e->segmentIsExponential = false;
			}
			return;
		case TSF_SEGMENT_RELEASE:
		default:
			e->segment = TSF_SEGMENT_DONE;
			e->segmentIsExponential = false;
			e->level = e->slope = 0.0f;
			e->samplesUntilNextSegment = 0x7FFFFFF;
	}
}

static void tsf_voice_envelope_setup(struct tsf_voice_envelope* e, struct tsf_envelope* new_parameters, int midiNoteNumber, short midiVelocity, bool isAmpEnv, float outSampleRate)
{
	e->parameters = *new_parameters;
	if (e->parameters.keynumToHold)
	{
		e->parameters.hold += e->parameters.keynumToHold * (60.0f - midiNoteNumber);
		e->parameters.hold = (e->parameters.hold < -10000.0f ? 0.0f : tsf_timecents2Secsf(e->parameters.hold));
	}
	if (e->parameters.keynumToDecay)
	{
		e->parameters.decay += e->parameters.keynumToDecay * (60.0f - midiNoteNumber);
		e->parameters.decay = (e->parameters.decay < -10000.0f ? 0.0f : tsf_timecents2Secsf(e->parameters.decay));
	}
	e->midiVelocity = midiVelocity;
	e->isAmpEnv = isAmpEnv;
	tsf_voice_envelope_nextsegment(e, TSF_SEGMENT_NONE, outSampleRate);
}

static void tsf_voice_envelope_process(struct tsf_voice_envelope* e, int numSamples, float outSampleRate) {
  if (e->slope) {
    if (e->segmentIsExponential) e->level *= powf(e->slope, (float)numSamples);
    else e->level += (e->slope * numSamples);
  }
  if ((e->samplesUntilNextSegment -= numSamples) <= 0) {
    tsf_voice_envelope_nextsegment(e, e->segment, outSampleRate);
  }
}

static void tsf_voice_lowpass_setup(struct tsf_voice_lowpass* e, float Fc) {
  // Lowpass filter from http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
  double K = tan(M_PI * Fc), KK = K * K;
  double norm = 1 / (1 + K * e->QInv + KK);
  e->a0 = KK * norm;
  e->a1 = 2 * e->a0;
  e->b1 = 2 * (KK - 1) * norm;
  e->b2 = (1 - K * e->QInv + KK) * norm;
}

static float tsf_voice_lowpass_process(struct tsf_voice_lowpass* e, double In) {
  double Out = In * e->a0 + e->z1; e->z1 = In * e->a1 + e->z2 - e->b1 * Out; e->z2 = In * e->a0 - e->b2 * Out; return (float)Out;
}

static void tsf_voice_lfo_setup(struct tsf_voice_lfo* e, float delay, int freqCents, float outSampleRate) {
  e->samplesUntil = (int)(delay * outSampleRate);
  e->delta = (4.0f * tsf_cents2Hertz((float)freqCents) / outSampleRate);
  e->level = 0;
}

static void tsf_voice_lfo_process(struct tsf_voice_lfo* e, int blockSamples) {
  if (e->samplesUntil > blockSamples) { e->samplesUntil -= blockSamples; return; }
  e->level += e->delta * blockSamples;
  if      (e->level >  1.0f) { e->delta = -e->delta; e->level =  2.0f - e->level; }
  else if (e->level < -1.0f) { e->delta = -e->delta; e->level = -2.0f - e->level; }
}

static void tsf_voice_kill(struct tsf_voice* v) {
  v->playingPreset = -1;
}

static void tsf_voice_calcpitchratio(struct tsf_voice* v, float pitchShift, float outSampleRate) {
  double note = v->apparentPlayingKey + v->region->transpose + v->region->tune / 100.0;
  double adjustedPitch = v->region->pitch_keycenter + (note - v->region->pitch_keycenter) * (v->region->pitch_keytrack / 100.0);
  if (pitchShift) adjustedPitch += pitchShift;
  v->pitchInputTimecents = adjustedPitch * 100.0;
  v->pitchOutputFactor = v->region->sample_rate / (tsf_timecents2Secsd(v->region->pitch_keycenter * 100.0) * outSampleRate);
}

static void tsf_voice_render(tsf* f, struct tsf_voice* v, float* outputBuffer, int numSamples) {
  struct tsf_region* region = v->region;
  float* input = f->fontSamples;
  float* outL = outputBuffer;
  float* outR = (f->outputmode == TSF_STEREO_UNWEAVED ? outL + numSamples : NULL);

  // Cache some values, to give them at least some chance of ending up in registers.
  bool updateModEnv = (region->modEnvToPitch || region->modEnvToFilterFc);
  bool updateModLFO = (v->modlfo.delta && (region->modLfoToPitch || region->modLfoToFilterFc || region->modLfoToVolume));
  bool updateVibLFO = (v->viblfo.delta && (region->vibLfoToPitch));
  bool isLooping    = (v->loopStart < v->loopEnd);
  unsigned int tmpLoopStart = v->loopStart, tmpLoopEnd = v->loopEnd;
  double tmpSampleEndDbl = (double)region->end, tmpLoopEndDbl = (double)tmpLoopEnd + 1.0;
  double tmpSourceSamplePosition = v->sourceSamplePosition;
  struct tsf_voice_lowpass tmpLowpass = v->lowpass;

  bool dynamicLowpass = (region->modLfoToFilterFc || region->modEnvToFilterFc);
  float tmpSampleRate = f->outSampleRate, tmpInitialFilterFc, tmpModLfoToFilterFc, tmpModEnvToFilterFc;
  
  bool dynamicPitchRatio = (region->modLfoToPitch || region->modEnvToPitch || region->vibLfoToPitch);
  double pitchRatio;
  float tmpModLfoToPitch, tmpVibLfoToPitch, tmpModEnvToPitch;
  
  bool dynamicGain = (region->modLfoToVolume != 0);
  float noteGain = 0, tmpModLfoToVolume;
  
  if (dynamicLowpass) tmpInitialFilterFc = (float)region->initialFilterFc, tmpModLfoToFilterFc = (float)region->modLfoToFilterFc, tmpModEnvToFilterFc = (float)region->modEnvToFilterFc;
  else tmpInitialFilterFc = 0, tmpModLfoToFilterFc = 0, tmpModEnvToFilterFc = 0;

  if (dynamicPitchRatio) pitchRatio = 0, tmpModLfoToPitch = (float)region->modLfoToPitch, tmpVibLfoToPitch = (float)region->vibLfoToPitch, tmpModEnvToPitch = (float)region->modEnvToPitch;
  else pitchRatio = tsf_timecents2Secsd(v->pitchInputTimecents) * v->pitchOutputFactor, tmpModLfoToPitch = 0, tmpVibLfoToPitch = 0, tmpModEnvToPitch = 0;
  
  if (dynamicGain) tmpModLfoToVolume = (float)region->modLfoToVolume * 0.1f;
  else noteGain = tsf_decibelsToGain(v->noteGainDB), tmpModLfoToVolume = 0;
  
  while (numSamples) {
    float gainMono, gainLeft, gainRight;
    int blockSamples = (numSamples > TSF_RENDER_EFFECTSAMPLEBLOCK ? TSF_RENDER_EFFECTSAMPLEBLOCK : numSamples);
    numSamples -= blockSamples;

    if (dynamicLowpass) {
      float fres = tmpInitialFilterFc + v->modlfo.level * tmpModLfoToFilterFc + v->modenv.level * tmpModEnvToFilterFc;
      float lowpassFc = (fres <= 13500 ? tsf_cents2Hertz(fres) / tmpSampleRate : 1.0f);
      tmpLowpass.active = (lowpassFc < 0.499f);
      if (tmpLowpass.active) tsf_voice_lowpass_setup(&tmpLowpass, lowpassFc);
    }

    if (dynamicPitchRatio) {
      pitchRatio = tsf_timecents2Secsd(v->pitchInputTimecents + (v->modlfo.level * tmpModLfoToPitch + v->viblfo.level * tmpVibLfoToPitch + v->modenv.level * tmpModEnvToPitch)) * v->pitchOutputFactor;
    }

    if (dynamicGain) {
      noteGain = tsf_decibelsToGain(v->noteGainDB + (v->modlfo.level * tmpModLfoToVolume));
    }

    gainMono = noteGain * v->ampenv.level;
    
    // Update EG.
    tsf_voice_envelope_process(&v->ampenv, blockSamples, tmpSampleRate);
    if (updateModEnv) tsf_voice_envelope_process(&v->modenv, blockSamples, tmpSampleRate);
    
    // Update LFOs.
    if (updateModLFO) tsf_voice_lfo_process(&v->modlfo, blockSamples);
    if (updateVibLFO) tsf_voice_lfo_process(&v->viblfo, blockSamples);
    
    switch (f->outputmode) {
    case TSF_STEREO_INTERLEAVED:
      gainLeft = gainMono * v->panFactorLeft, gainRight = gainMono * v->panFactorRight;
      while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndDbl) {
	unsigned int pos = (unsigned int)tmpSourceSamplePosition, nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
	
	// Simple linear interpolation.
	float alpha = (float)(tmpSourceSamplePosition - pos), val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);
	
	// Low-pass filter.
	if (tmpLowpass.active) val = tsf_voice_lowpass_process(&tmpLowpass, val);
	
	*outL++ += val * gainLeft;
	*outL++ += val * gainRight;
	
	// Next sample.
	tmpSourceSamplePosition += pitchRatio;
	if (tmpSourceSamplePosition >= tmpLoopEndDbl && isLooping) tmpSourceSamplePosition -= (tmpLoopEnd - tmpLoopStart + 1.0);
      }
      break;
      
    case TSF_STEREO_UNWEAVED:
      gainLeft = gainMono * v->panFactorLeft, gainRight = gainMono * v->panFactorRight;
      while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndDbl) {
	unsigned int pos = (unsigned int)tmpSourceSamplePosition, nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
	
	// Simple linear interpolation.
	float alpha = (float)(tmpSourceSamplePosition - pos), val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);
	
	// Low-pass filter.
	if (tmpLowpass.active) val = tsf_voice_lowpass_process(&tmpLowpass, val);
	
	*outL++ += val * gainLeft;
	*outR++ += val * gainRight;
	
	// Next sample.
	tmpSourceSamplePosition += pitchRatio;
	if (tmpSourceSamplePosition >= tmpLoopEndDbl && isLooping) tmpSourceSamplePosition -= (tmpLoopEnd - tmpLoopStart + 1.0);
      }
      break;
      
    case TSF_MONO:
      while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndDbl) {
	unsigned int pos = (unsigned int)tmpSourceSamplePosition, nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
	
	// Simple linear interpolation.
	float alpha = (float)(tmpSourceSamplePosition - pos), val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);
	
	// Low-pass filter.
	if (tmpLowpass.active) val = tsf_voice_lowpass_process(&tmpLowpass, val);
	
	*outL++ += val * gainMono;
	
	// Next sample.
	tmpSourceSamplePosition += pitchRatio;
	if (tmpSourceSamplePosition >= tmpLoopEndDbl && isLooping) tmpSourceSamplePosition -= (tmpLoopEnd - tmpLoopStart + 1.0);
      }
      break;
    }
    
    if (tmpSourceSamplePosition >= tmpSampleEndDbl || v->ampenv.segment == TSF_SEGMENT_DONE) {
      tsf_voice_kill(v);
      return;
    }
  }

  v->sourceSamplePosition = tmpSourceSamplePosition;
  if (tmpLowpass.active || dynamicLowpass) v->lowpass = tmpLowpass;
}

tsf* tsf_load(struct tsf_stream* stream)
{
	tsf* res = NULL;
	struct tsf_riffchunk chunkHead;
	struct tsf_riffchunk chunkList;
	struct tsf_hydra hydra;
	float* fontSamples = NULL;
	unsigned int fontSampleCount = 0;

	if (!tsf_riffchunk_read(NULL, &chunkHead, stream) || !TSF_FourCCEquals(chunkHead.id, "sfbk"))
	{
		//if (e) *e = TSF_INVALID_NOSF2HEADER;
		return res;
	}

	// Read hydra and locate sample data.
	memset(&hydra, 0, sizeof(hydra));
	while (tsf_riffchunk_read(&chunkHead, &chunkList, stream))
	{
		struct tsf_riffchunk chunk;
		if (TSF_FourCCEquals(chunkList.id, "pdta"))
		{
			while (tsf_riffchunk_read(&chunkList, &chunk, stream))
			{
				#define HandleChunk(chunkName) (TSF_FourCCEquals(chunk.id, #chunkName) && !(chunk.size % chunkName##SizeInFile)) \
					{ \
						int num = chunk.size / chunkName##SizeInFile, i; \
						hydra.chunkName##Num = num; \
						hydra.chunkName##s = (struct tsf_hydra_##chunkName*)malloc(num * sizeof(struct tsf_hydra_##chunkName)); \
						for (i = 0; i < num; ++i) tsf_hydra_read_##chunkName(&hydra.chunkName##s[i], stream); \
					}
				enum
				{
					phdrSizeInFile = 38, pbagSizeInFile =  4, pmodSizeInFile = 10,
					pgenSizeInFile =  4, instSizeInFile = 22, ibagSizeInFile =  4,
					imodSizeInFile = 10, igenSizeInFile =  4, shdrSizeInFile = 46
				};
				if      HandleChunk(phdr) else if HandleChunk(pbag) else if HandleChunk(pmod)
				else if HandleChunk(pgen) else if HandleChunk(inst) else if HandleChunk(ibag)
				else if HandleChunk(imod) else if HandleChunk(igen) else if HandleChunk(shdr)
				else stream->skip(stream->data, chunk.size);
				#undef HandleChunk
			}
		}
		else if (TSF_FourCCEquals(chunkList.id, "sdta"))
		{
			while (tsf_riffchunk_read(&chunkList, &chunk, stream))
			{
				if (TSF_FourCCEquals(chunk.id, "smpl"))
				{
					tsf_load_samples(&fontSamples, &fontSampleCount, &chunk, stream);
				}
				else stream->skip(stream->data, chunk.size);
			}
		}
		else stream->skip(stream->data, chunkList.size);
	}
	if (!hydra.phdrs || !hydra.pbags || !hydra.pmods || !hydra.pgens || !hydra.insts || !hydra.ibags || !hydra.imods || !hydra.igens || !hydra.shdrs)
	{
		//if (e) *e = TSF_INVALID_INCOMPLETE;
	}
	else if (fontSamples == NULL)
	{
		//if (e) *e = TSF_INVALID_NOSAMPLEDATA;
	}
	else
	{
		res = (tsf*)malloc(sizeof(tsf));
		memset(res, 0, sizeof(tsf));
		res->presetNum = hydra.phdrNum - 1;
		res->presets = (struct tsf_preset*)malloc(res->presetNum * sizeof(struct tsf_preset));
		res->fontSamples = fontSamples;
		res->outSampleRate = 44100.0f;
		fontSamples = NULL; //don't free below
		tsf_load_presets(res, &hydra, fontSampleCount);
	}
	free(hydra.phdrs); free(hydra.pbags); free(hydra.pmods);
	free(hydra.pgens); free(hydra.insts); free(hydra.ibags);
	free(hydra.imods); free(hydra.igens); free(hydra.shdrs);
	free(fontSamples);
	return res;
}

void tsf_close(tsf* f) {
  struct tsf_preset *preset, *presetEnd;
  if (!f) return;
  for (preset = f->presets, presetEnd = preset + f->presetNum; preset != presetEnd; preset++) {
    free(preset->regions);
  }
  free(f->presets);
  free(f->fontSamples);
  if (f->channels) { free(f->channels->channels); free(f->channels); }
  free(f->outputSamples);
  free(f);
}

#if 0
// Stop all playing notes immediatly and reset all channel parameters

void tsf_reset(tsf* f)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
		if (v->playingPreset != -1 && (v->ampenv.segment < TSF_SEGMENT_RELEASE || v->ampenv.parameters.release))
			tsf_voice_endquick(f, v);
	if (f->channels) { free(f->channels->channels); free(f->channels); f->channels = NULL; }
}
#endif

int tsf_get_presetindex(const tsf* f, int bank, int preset_number)
{
	const struct tsf_preset *presets;
	int i, iMax;
	for (presets = f->presets, i = 0, iMax = f->presetNum; i < iMax; i++)
		if (presets[i].preset == preset_number && presets[i].bank == bank)
			return i;
	return -1;
}

int tsf_get_presetcount(const tsf* f)
{
	return f->presetNum;
}

const char* tsf_get_presetname(const tsf* f, int preset)
{
	return (preset < 0 || preset >= f->presetNum ? NULL : f->presets[preset].presetName);
}

const char* tsf_bank_get_presetname(const tsf* f, int bank, int preset_number)
{
	return tsf_get_presetname(f, tsf_get_presetindex(f, bank, preset_number));
}

// Set the global gain as a volume factor
//   global_gain: the desired volume where 1.0 is 100%

void tsf_set_volume(tsf* f, float global_volume)
{
	f->globalGainDB = (global_volume == 1.0f ? 0 : -tsf_gainToDecibels(1.0f / global_volume));
}

#if 0
// Start playing a note
//   preset_index: preset index >= 0 and < tsf_get_presetcount()
//   key: note value between 0 and 127 (60 being middle C)
//   vel: velocity as a float between 0.0 (equal to note off) and 1.0 (full)
//   bank: instrument bank number (alternative to preset_index)
//   preset_number: preset number (alternative to preset_index)
//   (bank_note_on returns 0 if preset does not exist, otherwise 1)
// void tsf_note_on(tsf* f, int preset_index, int key, float vel);

void tsf_note_on(tsf* f, int preset_index, int key, float vel)
{
	short midiVelocity = (short)(vel * 127);
	int voicePlayIndex;
	struct tsf_region *region, *regionEnd;

	if (preset_index < 0 || preset_index >= f->presetNum) return;
	if (vel <= 0.0f) { tsf_note_off(f, preset_index, key); return; }

	// Play all matching regions.
	voicePlayIndex = f->voicePlayIndex++;
	for (region = f->presets[preset_index].regions, regionEnd = region + f->presets[preset_index].regionNum; region != regionEnd; region++)
	{
		struct tsf_voice *voice, *v, *vEnd; bool doLoop; float lowpassFilterQDB, lowpassFc;
		if (key < region->lokey || key > region->hikey || midiVelocity < region->lovel || midiVelocity > region->hivel) continue;

		voice = NULL, v = f->voices, vEnd = v + f->voiceNum;
		if (region->group)
		{
			for (; v != vEnd; v++)
				if (v->playingPreset == preset_index && v->region->group == region->group) tsf_voice_endquick(f, v);
				else if (v->playingPreset == -1 && !voice) voice = v;
		}
		else for (; v != vEnd; v++) if (v->playingPreset == -1) { voice = v; break; }

		if (!voice)
		{
			if (f->maxVoiceNum)
			{
				// voices have been pre-allocated and limited to a maximum, unable to start playing this voice
				continue;
			}
			f->voiceNum += 4;
			f->voices = (struct tsf_voice*)realloc(f->voices, f->voiceNum * sizeof(struct tsf_voice));
			voice = &f->voices[f->voiceNum - 4];
			voice[1].playingPreset = voice[2].playingPreset = voice[3].playingPreset = -1;
		}

		voice->region = region;
		voice->playingPreset = preset_index;
		voice->playingKey = key;
		voice->playIndex = voicePlayIndex;
		voice->noteGainDB = f->globalGainDB - region->attenuation - tsf_gainToDecibels(1.0f / vel);

		if (f->channels)
		{
			f->channels->setupVoice(f, voice);
		}
		else
		{
			tsf_voice_calcpitchratio(voice, 0, f->outSampleRate);
			// The SFZ spec is silent about the pan curve, but a 3dB pan law seems common. This sqrt() curve matches what Dimension LE does; Alchemy Free seems closer to sin(adjustedPan * pi/2).
			voice->panFactorLeft  = sqrtf(0.5f - region->pan);
			voice->panFactorRight = sqrtf(0.5f + region->pan);
		}

		// Offset/end.
		voice->sourceSamplePosition = region->offset;

		// Loop.
		doLoop = (region->loop_mode != TSF_LOOPMODE_NONE && region->loop_start < region->loop_end);
		voice->loopStart = (doLoop ? region->loop_start : 0);
		voice->loopEnd = (doLoop ? region->loop_end : 0);

		// Setup envelopes.
		tsf_voice_envelope_setup(&voice->ampenv, &region->ampenv, key, midiVelocity, true, f->outSampleRate);
		tsf_voice_envelope_setup(&voice->modenv, &region->modenv, key, midiVelocity, false, f->outSampleRate);

		// Setup lowpass filter.
		lowpassFc = (region->initialFilterFc <= 13500 ? tsf_cents2Hertz((float)region->initialFilterFc) / f->outSampleRate : 1.0f);
		lowpassFilterQDB = region->initialFilterQ / 10.0f;
		voice->lowpass.QInv = 1.0 / pow(10.0, (lowpassFilterQDB / 20.0));
		voice->lowpass.z1 = voice->lowpass.z2 = 0;
		voice->lowpass.active = (lowpassFc < 0.499f);
		if (voice->lowpass.active) tsf_voice_lowpass_setup(&voice->lowpass, lowpassFc);

		// Setup LFO filters.
		tsf_voice_lfo_setup(&voice->modlfo, region->delayModLFO, region->freqModLFO, f->outSampleRate);
		tsf_voice_lfo_setup(&voice->viblfo, region->delayVibLFO, region->freqVibLFO, f->outSampleRate);
	}
}
#endif

#if 0

// Stop playing a note
//   (bank_note_off returns 0 if preset does not exist, otherwise 1)

int tsf_bank_note_on(tsf* f, int bank, int preset_number, int key, float vel)
{
	int preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index == -1) return 0;
	tsf_note_on(f, preset_index, key, vel);
	return 1;
}

#endif

#if 0

void tsf_note_off(tsf* f, int preset_index, int key) {
  struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum, *vMatchFirst = NULL, *vMatchLast = NULL;
  for (; v != vEnd; v++) {
    // Find the first and last entry in the voices list with matching preset, key and look up the smallest play index
    
    if (v->playingPreset != preset_index || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE) continue;
    else if (!vMatchFirst || v->playIndex < vMatchFirst->playIndex) vMatchFirst = vMatchLast = v;
    else if (v->playIndex == vMatchFirst->playIndex) vMatchLast = v;
  }
  if (!vMatchFirst) return;
  for (v = vMatchFirst; v <= vMatchLast; v++) {
    //Stop all voices with matching preset, key and the smallest play index which was enumerated above
    if (v != vMatchFirst && v != vMatchLast &&
	(v->playIndex != vMatchFirst->playIndex || v->playingPreset != preset_index || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE)) continue;
    tsf_voice_end(f, v);
  }
}

#endif

#if 0
int tsf_bank_note_off(tsf* f, int bank, int preset_number, int key)
{
	int preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index == -1) return 0;
	tsf_note_off(f, preset_index, key);
	return 1;
}
#endif

static void tsf_channel_setup_voice(tsf* f, struct tsf_voice* v) {
  struct tsf_channel* c = &f->channels->channels[f->channels->activeChannel];
  float newpan = v->region->pan + c->panOffset;
  v->playingChannel = f->channels->activeChannel;
  v->noteGainDB += c->gainDB;
  tsf_voice_calcpitchratio(v, (c->pitchWheel == 8192 ? c->tuning : ((c->pitchWheel / 16383.0f * c->pitchRange * 2.0f) - c->pitchRange + c->tuning)), f->outSampleRate);
  if (newpan <= -0.5f) { v->panFactorLeft = 1.0f; v->panFactorRight = 0.0f; }
  else if (newpan >=  0.5f) { v->panFactorLeft = 0.0f; v->panFactorRight = 1.0f; }
  else { v->panFactorLeft = sqrtf(0.5f - newpan); v->panFactorRight = sqrtf(0.5f + newpan); }
}

static struct tsf_channel* tsf_channel_init(tsf* f, int channel)
{
	int i;
	if (f->channels && channel < f->channels->channelNum) return &f->channels->channels[channel];
	if (!f->channels)
	{
		f->channels = (struct tsf_channels*)malloc(sizeof(struct tsf_channels));
		f->channels->setupVoice = &tsf_channel_setup_voice;
		f->channels->channels = NULL;
		f->channels->channelNum = 0;
		f->channels->activeChannel = 0;
	}
	i = f->channels->channelNum;
	f->channels->channelNum = channel + 1;
	f->channels->channels = (struct tsf_channel*)realloc(f->channels->channels, f->channels->channelNum * sizeof(struct tsf_channel));
	for (; i <= channel; i++)
	{
		struct tsf_channel* c = &f->channels->channels[i];
		c->presetIndex = c->bank = 0;
		c->pitchWheel = c->midiPan = 8192;
		c->midiVolume = c->midiExpression = 16383;
		c->midiRPN = 0xFFFF;
		c->midiData = 0;
		c->panOffset = 0.0f;
		c->gainDB = 0.0f;
		c->pitchRange = 2.0f;
		c->tuning = 0.0f;
	}
	return &f->channels->channels[channel];
}

#if 0
static void tsf_channel_applypitch(tsf* f, int channel, struct tsf_channel* c)
{
	struct tsf_voice *v, *vEnd;
	float pitchShift = (c->pitchWheel == 8192 ? c->tuning : ((c->pitchWheel / 16383.0f * c->pitchRange * 2.0f) - c->pitchRange + c->tuning));
	for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
		if (v->playingChannel == channel && v->playingPreset != -1)
			tsf_voice_calcpitchratio(v, pitchShift, f->outSampleRate);
}
#endif

void tsf_channel_set_presetindex(tsf* f, int channel, int preset_index)
{
	tsf_channel_init(f, channel)->presetIndex = (unsigned short)preset_index;
}

int tsf_channel_set_presetnumber(tsf* f, int channel, int preset_number, int flag_mididrums)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	int preset_index;
	if (flag_mididrums)
	{
		preset_index = tsf_get_presetindex(f, 128 | (c->bank & 0x7FFF), preset_number);
		if (preset_index == -1) preset_index = tsf_get_presetindex(f, 128, preset_number);
		if (preset_index == -1) preset_index = tsf_get_presetindex(f, 128, 0);
		if (preset_index == -1) preset_index = tsf_get_presetindex(f, (c->bank & 0x7FFF), preset_number);
	}
	else preset_index = tsf_get_presetindex(f, (c->bank & 0x7FFF), preset_number);
	if (preset_index == -1) preset_index = tsf_get_presetindex(f, 0, preset_number);
	if (preset_index != -1)
	{
		c->presetIndex = (unsigned short)preset_index;
		return 1;
	}
	return 0;
}

void tsf_channel_set_bank(tsf* f, int channel, int bank)
{
	tsf_channel_init(f, channel)->bank = (unsigned short)bank;
}

int tsf_channel_set_bank_preset(tsf* f, int channel, int bank, int preset_number)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	int preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index == -1) return 0;
	c->presetIndex = (unsigned short)preset_index;
	c->bank = (unsigned short)bank;
	return 1;
}

#if 0
void tsf_channel_set_pan(tsf* f, int channel, float pan)
{
	struct tsf_voice *v, *vEnd;
	for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
		if (v->playingChannel == channel && v->playingPreset != -1)
		{
			float newpan = v->region->pan + pan - 0.5f;
			if      (newpan <= -0.5f) { v->panFactorLeft = 1.0f; v->panFactorRight = 0.0f; }
			else if (newpan >=  0.5f) { v->panFactorLeft = 0.0f; v->panFactorRight = 1.0f; }
			else { v->panFactorLeft = sqrtf(0.5f - newpan); v->panFactorRight = sqrtf(0.5f + newpan); }
		}
	tsf_channel_init(f, channel)->panOffset = pan - 0.5f;
}
#endif

#if 0
void tsf_channel_set_volume(tsf* f, int channel, float volume)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	float gainDB = tsf_gainToDecibels(volume), gainDBChange = gainDB - c->gainDB;
	struct tsf_voice *v, *vEnd;
	if (gainDBChange == 0) return;
	for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
		if (v->playingChannel == channel && v->playingPreset != -1)
			v->noteGainDB += gainDBChange;
	c->gainDB = gainDB;
}
#endif

#if 0
void tsf_channel_set_pitchwheel(tsf* f, int channel, int pitch_wheel)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (c->pitchWheel == pitch_wheel) return;
	c->pitchWheel = (unsigned short)pitch_wheel;
	tsf_channel_applypitch(f, channel, c);
}

void tsf_channel_set_pitchrange(tsf* f, int channel, float pitch_range)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (c->pitchRange == pitch_range) return;
	c->pitchRange = pitch_range;
	if (c->pitchWheel != 8192) tsf_channel_applypitch(f, channel, c);
}

void tsf_channel_set_tuning(tsf* f, int channel, float tuning)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (c->tuning == tuning) return;
	c->tuning = tuning;
	tsf_channel_applypitch(f, channel, c);
}
#endif

#if 0

// Start or stop playing notes on a channel (needs channel preset to be set)
//   channel: channel number
//   key: note value between 0 and 127 (60 being middle C)
//   vel: velocity as a float between 0.0 (equal to note off) and 1.0 (full)

void tsf_channel_note_on(tsf* f, int channel, int key, float vel)
{
	if (!f->channels || channel >= f->channels->channelNum) return;
	f->channels->activeChannel = channel;
	tsf_note_on(f, f->channels->channels[channel].presetIndex, key, vel);
}

#endif

#if 0
void tsf_channel_note_off(tsf* f, int channel, int key)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum, *vMatchFirst = NULL, *vMatchLast = NULL;
	for (; v != vEnd; v++)
	{
		//Find the first and last entry in the voices list with matching channel, key and look up the smallest play index
		if (v->playingPreset == -1 || v->playingChannel != channel || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE) continue;
		else if (!vMatchFirst || v->playIndex < vMatchFirst->playIndex) vMatchFirst = vMatchLast = v;
		else if (v->playIndex == vMatchFirst->playIndex) vMatchLast = v;
	}
	if (!vMatchFirst) return;
	for (v = vMatchFirst; v <= vMatchLast; v++)
	{
		//Stop all voices with matching channel, key and the smallest play index which was enumerated above
		if (v != vMatchFirst && v != vMatchLast &&
			(v->playIndex != vMatchFirst->playIndex || v->playingPreset == -1 || v->playingChannel != channel || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE)) continue;
		tsf_voice_end(f, v);
	}
}
#endif

#if 0

// Apply a MIDI control change to the channel (not all controllers are supported!)

void tsf_channel_midi_control(tsf* f, int channel, int controller, int control_value) {
	struct tsf_channel* c = tsf_channel_init(f, channel);
	switch (controller)
	{
		case   7 /*VOLUME_MSB*/      : c->midiVolume     = (unsigned short)((c->midiVolume     & 0x7F  ) | (control_value << 7)); goto TCMC_SET_VOLUME;
		case  39 /*VOLUME_LSB*/      : c->midiVolume     = (unsigned short)((c->midiVolume     & 0x3F80) |  control_value);       goto TCMC_SET_VOLUME;
		case  11 /*EXPRESSION_MSB*/  : c->midiExpression = (unsigned short)((c->midiExpression & 0x7F  ) | (control_value << 7)); goto TCMC_SET_VOLUME;
		case  43 /*EXPRESSION_LSB*/  : c->midiExpression = (unsigned short)((c->midiExpression & 0x3F80) |  control_value);       goto TCMC_SET_VOLUME;
		case  10 /*PAN_MSB*/         : c->midiPan        = (unsigned short)((c->midiPan        & 0x7F  ) | (control_value << 7)); goto TCMC_SET_PAN;
		case  42 /*PAN_LSB*/         : c->midiPan        = (unsigned short)((c->midiPan        & 0x3F80) |  control_value);       goto TCMC_SET_PAN;
		case   6 /*DATA_ENTRY_MSB*/  : c->midiData       = (unsigned short)((c->midiData       & 0x7F)   | (control_value << 7)); goto TCMC_SET_DATA;
		case  38 /*DATA_ENTRY_LSB*/  : c->midiData       = (unsigned short)((c->midiData       & 0x3F80) |  control_value);       goto TCMC_SET_DATA;
		case   0 /*BANK_SELECT_MSB*/ : c->bank = (unsigned short)(0x8000 | control_value); return; //bank select MSB alone acts like LSB
		case  32 /*BANK_SELECT_LSB*/ : c->bank = (unsigned short)((c->bank & 0x8000 ? ((c->bank & 0x7F) << 7) : 0) | control_value); return;
		case 101 /*RPN_MSB*/         : c->midiRPN = (unsigned short)(((c->midiRPN == 0xFFFF ? 0 : c->midiRPN) & 0x7F  ) | (control_value << 7)); return;
		case 100 /*RPN_LSB*/         : c->midiRPN = (unsigned short)(((c->midiRPN == 0xFFFF ? 0 : c->midiRPN) & 0x3F80) |  control_value); return;
		case  98 /*NRPN_LSB*/        : c->midiRPN = 0xFFFF; return;
		case  99 /*NRPN_MSB*/        : c->midiRPN = 0xFFFF; return;
		case 120 /*ALL_SOUND_OFF*/   : tsf_channel_sounds_off_all(f, channel); return;
		case 123 /*ALL_NOTES_OFF*/   : tsf_channel_note_off_all(f, channel);   return;
		case 121 /*ALL_CTRL_OFF*/    :
			c->midiVolume = c->midiExpression = 16383;
			c->midiPan = 8192;
			c->bank = 0;
			tsf_channel_set_volume(f, channel, 1.0f);
			tsf_channel_set_pan(f, channel, 0.5f);
			tsf_channel_set_pitchrange(f, channel, 2.0f);
			return;
	}
	return;
	
TCMC_SET_VOLUME:
	//Raising to the power of 3 seems to result in a decent sounding volume curve for MIDI
	tsf_channel_set_volume(f, channel, powf((c->midiVolume / 16383.0f) * (c->midiExpression / 16383.0f), 3.0f));
	return;
	
TCMC_SET_PAN:
	tsf_channel_set_pan(f, channel, c->midiPan / 16383.0f);
	return;
	
TCMC_SET_DATA:
	if      (c->midiRPN == 0) tsf_channel_set_pitchrange(f, channel, (c->midiData >> 7) + 0.01f * (c->midiData & 0x7F));
	else if (c->midiRPN == 1) tsf_channel_set_tuning(f, channel, (int)c->tuning + ((float)c->midiData - 8192.0f) / 8192.0f); //fine tune
	else if (c->midiRPN == 2 && controller == 6) tsf_channel_set_tuning(f, channel, ((float)control_value - 64.0f) + (c->tuning - (int)c->tuning)); //coarse tune
	return;
}

#endif

void
SoundFontInstrument::openFile() {
  tsf_handle = tsf_load_filename(filename.c_str());

  tsf_handle->outputmode = TSF_MONO;
  tsf_handle->outSampleRate = 44100.0f;
  tsf_handle->globalGainDB = 0.0f;
}

class SoundFontInstrumentVoice : public InstrumentVoice {
public:
  SoundFontInstrumentVoice() {
    voice = new tsf_voice;
    voice->playingPreset = -1;
  }
  ~SoundFontInstrumentVoice() {
    delete voice;
  }

  void playNote(Note note, int transpose, int detune) override;
  bool isPlaying() const override {
    return voice->playingPreset != -1;
  }

  void stopNote() {
    assert(f && voice);
    
    tsf_voice_envelope_nextsegment(&voice->ampenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
    tsf_voice_envelope_nextsegment(&voice->modenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
    if (voice->region->loop_mode == TSF_LOOPMODE_SUSTAIN) {
      // Continue playing, but stop looping.
      voice->loopEnd = voice->loopStart;
    }
  }

  void stopNoteQuick() {
    assert(f && voice);

    voice->ampenv.parameters.release = 0.0f;
    tsf_voice_envelope_nextsegment(&voice->ampenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
    
    voice->modenv.parameters.release = 0.0f;
    tsf_voice_envelope_nextsegment(&voice->modenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
  }

  tsf * f = 0;
  tsf_voice * voice = 0;
  int preset;
};

float
SoundFontInstrument::getSample(InstrumentVoice & _voice) const {
  float buffer[1];
  buffer[0] = 0;
  
  auto & voice = dynamic_cast<SoundFontInstrumentVoice&>(_voice);
  
  tsf_voice_render(tsf_handle, voice.voice, buffer, 1);
  
  return buffer[0];
}

std::shared_ptr<InstrumentVoice>
SoundFontInstrument::createVoice() const {
  auto voice = make_shared<SoundFontInstrumentVoice>();
  voice->f = tsf_handle;
  voice->preset = preset;
  return voice;
}

void
SoundFontInstrumentVoice::playNote(Note note, int transpose, int detune) {
  if (note.isOff()) {
    stopNote();    
    return;
  }
  
  // short midiVelocity = 63; // (short)(vel * 127);  
  double frequency = note.getFrequency(transpose, detune);
    
  int preset_index = preset;
  if (preset_index < 0 || preset_index >= f->presetNum) return;

  // int key = note.getMidiNote();
  double apparent_key = log2(frequency / 440) * 12 + 69;
  int integer_key = int(apparent_key + 0);
    
  short midiVelocity = note.getVelocity();
  float vel = midiVelocity / 127.0f;

  // Play all matching regions.
  struct tsf_region *region, *regionEnd;

  for (region = f->presets[preset_index].regions, regionEnd = region + f->presets[preset_index].regionNum; region != regionEnd; region++) {
    bool doLoop;
    float lowpassFilterQDB, lowpassFc;
    
    if (integer_key < region->lokey || integer_key > region->hikey || midiVelocity < region->lovel || midiVelocity > region->hivel) continue;
    
    if (region->group) {
      assert(0);
    }
    
    voice->region = region;
    voice->playingPreset = preset_index;
    voice->apparentPlayingKey = apparent_key;
    // voice->playingFrequency = frequency;
    voice->noteGainDB = f->globalGainDB - region->attenuation - tsf_gainToDecibels(1.0f / vel);
    
    if (f->channels) {
      f->channels->setupVoice(f, voice);
    } else {
      tsf_voice_calcpitchratio(voice, 0, f->outSampleRate);
      // The SFZ spec is silent about the pan curve, but a 3dB pan law seems common. This sqrt() curve matches what Dimension LE does; Alchemy Free seems closer to sin(adjustedPan * pi/2).
      voice->panFactorLeft  = sqrtf(0.5f - region->pan);
      voice->panFactorRight = sqrtf(0.5f + region->pan);
    }
    
    // Offset/end.
    voice->sourceSamplePosition = region->offset;
    
    // Loop.
    doLoop = (region->loop_mode != TSF_LOOPMODE_NONE && region->loop_start < region->loop_end);
    voice->loopStart = (doLoop ? region->loop_start : 0);
    voice->loopEnd = (doLoop ? region->loop_end : 0);
    
    // Setup envelopes.
    tsf_voice_envelope_setup(&voice->ampenv, &region->ampenv, apparent_key, midiVelocity, true, f->outSampleRate);
    tsf_voice_envelope_setup(&voice->modenv, &region->modenv, apparent_key, midiVelocity, false, f->outSampleRate);
    
    // Setup lowpass filter.
    lowpassFc = (region->initialFilterFc <= 13500 ? tsf_cents2Hertz((float)region->initialFilterFc) / f->outSampleRate : 1.0f);
    lowpassFilterQDB = region->initialFilterQ / 10.0f;
    voice->lowpass.QInv = 1.0 / pow(10.0, (lowpassFilterQDB / 20.0));
    voice->lowpass.z1 = voice->lowpass.z2 = 0;
    voice->lowpass.active = (lowpassFc < 0.499f);
    if (voice->lowpass.active) tsf_voice_lowpass_setup(&voice->lowpass, lowpassFc);
    
    // Setup LFO filters.
    tsf_voice_lfo_setup(&voice->modlfo, region->delayModLFO, region->freqModLFO, f->outSampleRate);
    tsf_voice_lfo_setup(&voice->viblfo, region->delayVibLFO, region->freqVibLFO, f->outSampleRate);

    break;
  }
}

SoundFontInstrument::~SoundFontInstrument() {
  tsf_close(tsf_handle);
}
