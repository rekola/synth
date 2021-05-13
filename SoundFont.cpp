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

#include "SoundFont.h"

#include "LowpassFilter.h"
#include "FourCC.h"
#include "EnvelopeState.h"
#include "LFO.h"
#include "InstrumentVoice.h"
#include "SampleData.h"

using namespace std;

struct tsf_riffchunk {
  FourCC id;
  uint32_t size;
};

class tsf_region {
public:
  int loop_mode;
  unsigned int sample_rate;
  unsigned char lokey, hikey, lovel, hivel;
  unsigned int group, offset, end, loop_start, loop_end;
  int transpose, tune, pitch_keycenter, pitch_keytrack;
  float attenuation;
  float pan; // not supported
  Envelope ampenv, modenv;
  int initialFilterQ, initialFilterFc;
  int modEnvToPitch, modEnvToFilterFc, modLfoToFilterFc, modLfoToVolume;
  float delayModLFO;
  int freqModLFO, modLfoToPitch;
  float delayVibLFO;
  int freqVibLFO, vibLfoToPitch;

  void clear(bool for_relative) {
    // memset(i, 0, sizeof(struct tsf_region));
    loop_mode = 0;
    sample_rate = 0;
    lokey = lovel = 0;
    group = 0;
    offset = 0;
    end = 0;
    loop_start = loop_end = 0;
    transpose = 0;
    tune = 0;
    pitch_keytrack = 0;
    attenuation = 0;
    pan = 0;

    ampenv.delay = ampenv.attack = ampenv.hold = ampenv.decay = ampenv.release = 0;
    modenv.delay = modenv.attack = modenv.hold = modenv.decay = modenv.release = 0;

    initialFilterQ = initialFilterFc = 0;
    modEnvToPitch = modEnvToFilterFc = 0;
    modLfoToFilterFc = modLfoToVolume = 0;
    delayModLFO = 0;
    freqModLFO = modLfoToPitch = 0;
    delayVibLFO = 0;
    freqVibLFO = vibLfoToPitch = 0;
    
    hikey = hivel = 127;
    pitch_keycenter = 60; // C4
    
    if (for_relative) return;
    
    pitch_keytrack = 100;
    pitch_keycenter = -1;
    
    // SF2 defaults in timecents.
    ampenv.delay = ampenv.attack = ampenv.hold = ampenv.decay = ampenv.release = -12000.0f;
    modenv.delay = modenv.attack = modenv.hold = modenv.decay = modenv.release = -12000.0f;
    
    initialFilterFc = 13500;
    
    delayModLFO = -12000.0f;
    delayVibLFO = -12000.0f;
  }
};

struct tsf_preset {
  char presetName[20];
  uint16_t preset, bank;
  vector<tsf_region> regions;
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
void tsf_load(SoundFontFile * f, struct tsf_stream* stream);

static inline float tsf_cents2Hertz(float cents) { return 8.176f * powf(2.0f, cents / 1200.0f); }

// Returns the number of presets in the loaded SoundFont
// int tsf_get_presetcount(const SoundFontFile* f);

// Higher level channel based functions, set up channel parameters
//   channel: channel number
//   preset_index: preset index >= 0 and < tsf_get_presetcount()
//   preset_number: preset number (alternative to preset_index)
//   flag_mididrums: 0 for normal channels, otherwise apply MIDI drum channel rules
//   bank: instrument bank number (alternative to preset_index)
//   volume: linear volume scale factor (default 1.0 full)
//   pitch_wheel: pitch wheel position 0 to 16383 (default 8192 unpitched)
//   pitch_range: range of the pitch wheel in semitones (default 2.0, total +/- 2 semitones)
//   tuning: tuning of all playing voices in semitones (default 0.0, standard (A440) tuning)
//   (set_preset_number and set_bank_preset return 0 if preset does not exist, otherwise 1)

// The lower this block size is the more accurate the effects are.
// Increasing the value significantly lowers the CPU usage of the voice rendering.
// If LFO affects the low-pass filter it can be hearable even as low as 8.
#define TSF_RENDER_EFFECTSAMPLEBLOCK 64

#include <cstring>
#include <cmath>
#include <cstdio>

static int tsf_stream_stdio_read(FILE* f, void* ptr, unsigned int size) { return (int)fread(ptr, 1, size, f); }
static int tsf_stream_stdio_skip(FILE* f, unsigned int count) { return !fseek(f, count, SEEK_CUR); }

enum { TSF_LOOPMODE_NONE, TSF_LOOPMODE_CONTINUOUS, TSF_LOOPMODE_SUSTAIN };

struct tsf_hydra {
  struct tsf_hydra_phdr *phdrs; struct tsf_hydra_pbag *pbags; struct tsf_hydra_pmod *pmods;
  struct tsf_hydra_pgen *pgens; struct tsf_hydra_inst *insts; struct tsf_hydra_ibag *ibags;
  struct tsf_hydra_imod *imods; struct tsf_hydra_igen *igens; struct tsf_hydra_shdr *shdrs;
  int phdrNum, pbagNum, pmodNum, pgenNum, instNum, ibagNum, imodNum, igenNum, shdrNum;
};

union tsf_hydra_genamount { struct { uint8_t lo, hi; } range; int16_t shortAmount; uint16_t wordAmount; };
struct tsf_hydra_phdr { char presetName[20]; uint16_t preset, bank, presetBagNdx; uint32_t library, genre, morphology; };
struct tsf_hydra_pbag { uint16_t genNdx, modNdx; };
struct tsf_hydra_pmod { uint16_t modSrcOper, modDestOper; int16_t modAmount; uint16_t modAmtSrcOper, modTransOper; };
struct tsf_hydra_pgen { uint16_t genOper; union tsf_hydra_genamount genAmount; };
struct tsf_hydra_inst { char instName[20]; uint16_t instBagNdx; };
struct tsf_hydra_ibag { uint16_t instGenNdx, instModNdx; };
struct tsf_hydra_imod { uint16_t modSrcOper, modDestOper; int16_t modAmount; uint16_t modAmtSrcOper, modTransOper; };
struct tsf_hydra_igen { uint16_t genOper; union tsf_hydra_genamount genAmount; };
struct tsf_hydra_shdr { char sampleName[20]; uint32_t start, end, startLoop, endLoop, sampleRate; uint8_t originalPitch; int8_t pitchCorrection; uint16_t sampleLink, sampleType; };

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

static bool tsf_riffchunk_read(struct tsf_riffchunk* parent, struct tsf_riffchunk* chunk, struct tsf_stream* stream) {
  if (parent && sizeof(FourCC) + sizeof(uint32_t) > parent->size) return false;
  if (!stream->read(stream->data, &chunk->id, sizeof(FourCC)) || chunk->id.data()[0] <= ' ' || chunk->id.data()[0] >= 'z') return false;
  if (!stream->read(stream->data, &chunk->size, sizeof(uint32_t))) return false;
  if (parent && sizeof(FourCC) + sizeof(uint32_t) + chunk->size > parent->size) return false;
  if (parent) parent->size -= sizeof(FourCC) + sizeof(uint32_t) + chunk->size;
  bool IsRiff = chunk->id == "RIFF", IsList = chunk->id == "LIST";
  if (IsRiff && parent) return false; // not allowed
  if (!IsRiff && !IsList) return true; // custom type without sub type
  if (!stream->read(stream->data, &chunk->id, sizeof(FourCC)) || chunk->id.data()[0] <= ' ' || chunk->id.data()[0] >= 'z') return false;
  chunk->size -= sizeof(FourCC);
  return true;
}

static void tsf_region_operator(struct tsf_region* region, uint16_t genOper, union tsf_hydra_genamount* amount, struct tsf_region* merge_region) {
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
	GEN_INT_LIMIT12K     = 0x10, // min -12000, max 12000
	GEN_INT_LIMITFC      = 0x20, // min 1500, max 13500
	GEN_INT_LIMITQ       = 0x30, // min 0, max 960
	GEN_INT_LIMIT960     = 0x40, // min -960, max 960
	GEN_INT_LIMIT16K4500 = 0x50, // min -16000, max 4500
	GEN_FLOAT_LIMIT12K5K = 0x60, // min -12000, max 5000
	GEN_FLOAT_LIMIT12K8K = 0x70, // min -12000, max 8000
	GEN_FLOAT_LIMIT1200  = 0x80, // min -1200, max 1200
	GEN_FLOAT_LIMITPAN   = 0x90, // * .001f, min -.5f, max .5f,
	GEN_FLOAT_LIMITATTN  = 0xA0, // * .1f, min 0, max 144.0
	GEN_FLOAT_MAX1000    = 0xB0, // min 0, max 1000
	GEN_FLOAT_MAX1440    = 0xC0, // min 0, max 1440
	
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

static void tsf_region_envtosecs(Envelope * p, bool sustainIsGain) {
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
  else if (sustainIsGain) p->sustain = InstrumentVoice::decibelsToGain(-p->sustain / 10.0f);
  else p->sustain = 1.0f - (p->sustain / 1000.0f);
}

static void tsf_load_samples(float** fontSamples, unsigned int* fontSampleCount, struct tsf_riffchunk *chunkSmpl, struct tsf_stream* stream) {
  // Read sample data into float format buffer.
  unsigned int samplesToRead;
  unsigned int samplesLeft = *fontSampleCount = chunkSmpl->size / sizeof(short);
  float * out = *fontSamples = (float*)malloc(samplesLeft * sizeof(float));
  for (; samplesLeft; samplesLeft -= samplesToRead) {
    short sampleBuffer[1024], *in = sampleBuffer;
    samplesToRead = (samplesLeft > 1024 ? 1024 : samplesLeft);
    stream->read(stream->data, sampleBuffer, samplesToRead * sizeof(short));
      
    // Convert from signed 16-bit to float.
    for (unsigned int samplesToConvert = samplesToRead; samplesToConvert > 0; --samplesToConvert) {
      // If we ever need to compile for big-endian platforms, we'll need to byte-swap here.
      *out++ = (float)(*in++ / 32767.0);
    }
  }
}

class SoundFontFile {
public:
  SoundFontFile(std::string filename) {
    loadFile(filename);
  }
  ~SoundFontFile() {
    free(fontSamples);
  }

  // Directly load a SoundFont from a .sf2 file path
  void loadFile(const std::string & filename) {
    struct tsf_stream stream = { nullptr, (int(*)(void*,void*,unsigned int))&tsf_stream_stdio_read, (int(*)(void*,unsigned int))&tsf_stream_stdio_skip };
#if __STDC_WANT_SECURE_LIB__
    FILE * fh = nullptr;
    fopen_s(&fh, filename, "rb");
#else
    FILE * fh = fopen(filename.c_str(), "rb");
#endif
    if (!fh) {
      // if (e) *e = TSF_FILENOTFOUND;
      return;
    }
    stream.data = fh;
    tsf_load(this, &stream);
    fclose(fh);  
  }

  // Returns the name of a preset index >= 0 and < tsf_get_presetcount()
  string getPresetName(size_t index) const {
    if (index < presets.size()) return presets[index].presetName;
    else return "";
  }

  // Returns the preset index from a bank and preset number, or -1 if it does not exist in the loaded SoundFont

  int getPresetIndex(int bank, int preset_number) const {
    for (size_t i = 0; i < presets.size(); i++) {
      if (presets[i].preset == preset_number && presets[i].bank == bank) {
	return i;
      }
    }
    return -1;
  }

  size_t getPresetCount() const { return presets.size(); }
  
  std::vector<tsf_preset> presets;
  float * fontSamples = 0;
};

static void tsf_load_presets(SoundFontFile* res, struct tsf_hydra *hydra, unsigned int fontSampleCount) {
  enum { GenInstrument = 41, GenKeyRange = 43, GenVelRange = 44, GenSampleID = 53 };
  // Read each preset.
  struct tsf_hydra_phdr *pphdr, *pphdrMax;
  for (pphdr = hydra->phdrs, pphdrMax = pphdr + hydra->phdrNum - 1; pphdr != pphdrMax; pphdr++)
    {
      int sortedIndex = 0, region_index = 0;
      struct tsf_hydra_phdr *otherphdr;
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

      struct tsf_preset * preset = &(res->presets[sortedIndex]);
      
      memcpy(preset->presetName, pphdr->presetName, sizeof(preset->presetName));
      preset->presetName[sizeof(preset->presetName)-1] = '\0'; //should be zero terminated in source file but make sure
      preset->bank = pphdr->bank;
      preset->preset = pphdr->preset;

      size_t regionNum = 0;
      
      // count regions covered by this preset
      for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++) {
	unsigned char plokey = 0, phikey = 127, plovel = 0, phivel = 127;
	struct tsf_hydra_pgen *ppgen, *ppgenEnd; struct tsf_hydra_inst *pinst; struct tsf_hydra_ibag *pibag, *pibagEnd; struct tsf_hydra_igen *pigen, *pigenEnd;
	for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++) {
	  if (ppgen->genOper == GenKeyRange) { plokey = ppgen->genAmount.range.lo; phikey = ppgen->genAmount.range.hi; continue; }
	  if (ppgen->genOper == GenVelRange) { plovel = ppgen->genAmount.range.lo; phivel = ppgen->genAmount.range.hi; continue; }
	  if (ppgen->genOper != GenInstrument) continue;
	  if (ppgen->genAmount.wordAmount >= hydra->instNum) continue;
	  pinst = hydra->insts + ppgen->genAmount.wordAmount;
	  for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++) {
	    unsigned char ilokey = 0, ihikey = 127, ilovel = 0, ihivel = 127;
	    for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++) {
	      if (pigen->genOper == GenKeyRange) { ilokey = pigen->genAmount.range.lo; ihikey = pigen->genAmount.range.hi; continue; }
	      if (pigen->genOper == GenVelRange) { ilovel = pigen->genAmount.range.lo; ihivel = pigen->genAmount.range.hi; continue; }
	      if (pigen->genOper == GenSampleID && ihikey >= plokey && ilokey <= phikey && ihivel >= plovel && ilovel <= phivel) regionNum++;
	    }
	  }
	}
      }

      preset->regions.resize(regionNum);
      globalRegion.clear(true);
      
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
		  uint16_t whichInst = ppgen->genAmount.wordAmount;
		  if (whichInst >= hydra->instNum) continue;
		  
		  instRegion.clear(false);
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
			      tsf_region_operator(&zoneRegion, 0, nullptr, &presetRegion);
			      
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
			  else tsf_region_operator(&zoneRegion, pigen->genOper, &pigen->genAmount, nullptr);
			}
		      
		      // Handle instrument's global zone.
		      if (pibag == hydra->ibags + pinst->instBagNdx && !hadSampleID)
			instRegion = zoneRegion;
		      
		      // Modulators (TODO)
		      //if (ibag->instModNdx < ibag[1].instModNdx) addUnsupportedOpcode("any modulator");
		    }
		  hadGenInstrument = 1;
		}
	      else tsf_region_operator(&presetRegion, ppgen->genOper, &ppgen->genAmount, nullptr);
	    }
	  
	  // Modulators (TODO)
	  //if (pbag->modNdx < pbag[1].modNdx) addUnsupportedOpcode("any modulator");
	  
	  // Handle preset's global zone.
	  if (ppbag == hydra->pbags + pphdr->presetBagNdx && !hadGenInstrument)
	    globalRegion = presetRegion;
	}
    }
}

class SoundFontVoice : public InstrumentVoice {
public:
  SoundFontVoice(unsigned int _outSampleRate, int _identifier, std::shared_ptr<SoundFontFile> _sf, size_t _preset = 0, size_t _fixedMidiKey = 0)
    : InstrumentVoice(_outSampleRate, _identifier), sf(_sf), preset(_preset), fixedMidiKey(_fixedMidiKey) {
    playingPreset = -1;
  }
  
  void playNote(float frequency, float velocity, float delay, float detune, unsigned short subvoice = 0) override {
    assert(frequency > 0);

    auto f = sf.get();

    if (preset < 0 || preset >= f->presets.size()) return;

    double apparent_key = log2(frequency / 440) * 12 + 69;
    int midiKey = fixedMidiKey ? fixedMidiKey : int(apparent_key);
    short midiVelocity = (short)(velocity * 127);
    if (midiVelocity > 127) midiVelocity = 127;
  
    auto & regions = f->presets[preset].regions;
    assert(subvoice < regions.size());
    auto & region = regions[subvoice];
                
    voiceRegion = &region;
    playingPreset = preset;
    apparentPlayingKey = apparent_key;
    // voice->playingFrequency = frequency;
    setGainDB(- region.attenuation - gainToDecibels(1.0f / velocity));
    calcPitchRatio(0);
    
    // Offset/end.
    sourceSamplePosition = region.offset;
    
    // Loop.
    bool doLoop = (region.loop_mode != TSF_LOOPMODE_NONE && region.loop_start < region.loop_end);
    loopStart = (doLoop ? region.loop_start : 0);
    loopEnd = (doLoop ? region.loop_end : 0);
    
    // Setup envelopes.
    ampenv = EnvelopeState(getOutSampleRate(), region.ampenv, apparent_key, midiVelocity, true, delay);
    modenv = EnvelopeState(getOutSampleRate(), region.modenv, apparent_key, midiVelocity, false, delay);
    
    // Setup lowpass filter.
    float lowpassFc = (region.initialFilterFc <= 13500 ? tsf_cents2Hertz((float)region.initialFilterFc) / getOutSampleRate() : 1.0f);
    float lowpassFilterQDB = region.initialFilterQ / 10.0f;
    
    lowpass.QInv = 1.0 / pow(10.0, (lowpassFilterQDB / 20.0));
    lowpass.z1 = lowpass.z2 = 0;
    lowpass.active = (lowpassFc < 0.499f);
    if (lowpass.active) lowpass.setup(lowpassFc);
    
    // Setup LFO filters.
    modlfo = LFO(region.delayModLFO, tsf_cents2Hertz(region.freqModLFO), getOutSampleRate());
    viblfo = LFO(region.delayVibLFO, tsf_cents2Hertz(region.freqVibLFO), getOutSampleRate());
  }

  bool isPlaying() const override { return playingPreset != -1; }

  void killNote() override {
    playingPreset = -1;
  }

  SampleData render(size_t numSamples) override;
  
  void stopNote() override {
    InstrumentVoice::stopNote();
    if (voiceRegion->loop_mode == TSF_LOOPMODE_SUSTAIN) {
      // Continue playing, but stop looping.
      loopEnd = loopStart;
    }
  }

  void stopNoteQuick() {
    ampenv.parameters.release = 0.0f;
    ampenv.nextSegment(EnvelopeState::SUSTAIN);
    
    modenv.parameters.release = 0.0f;
    modenv.nextSegment(EnvelopeState::SUSTAIN);
  }

  void calcPitchRatio(float pitchShift) {
    double note = apparentPlayingKey + voiceRegion->transpose + voiceRegion->tune / 100.0;
    double adjustedPitch = voiceRegion->pitch_keycenter + (note - voiceRegion->pitch_keycenter) * (voiceRegion->pitch_keytrack / 100.0);
    if (pitchShift) adjustedPitch += pitchShift;
    pitchInputTimecents = adjustedPitch * 100.0;
    pitchOutputFactor = voiceRegion->sample_rate / (tsf_timecents2Secsd(voiceRegion->pitch_keycenter * 100.0) * getOutSampleRate());
  }
    
  int playingPreset;
  double apparentPlayingKey;
  struct tsf_region * voiceRegion;
  double pitchInputTimecents, pitchOutputFactor;
  unsigned int loopStart, loopEnd;
  LowpassFilter lowpass;
  LFO modlfo, viblfo;

private:
  shared_ptr<SoundFontFile> sf;
  size_t preset, fixedMidiKey;
};

SampleData
SoundFontVoice::render(size_t numSamples) {
  auto f = sf.get();
  
  SampleData outputData(1, numSamples);
  float * output = outputData.data();
    
  float * input = f->fontSamples;
  
  bool updateModEnv = (voiceRegion->modEnvToPitch || voiceRegion->modEnvToFilterFc);
  bool updateModLFO = (modlfo.getDelta() && (voiceRegion->modLfoToPitch || voiceRegion->modLfoToFilterFc || voiceRegion->modLfoToVolume));
  bool updateVibLFO = (viblfo.getDelta() && (voiceRegion->vibLfoToPitch));
  bool isLooping    = (loopStart < loopEnd);
  double sampleEndDbl = (double)voiceRegion->end;
  double loopEndDbl = (double)loopEnd + 1.0;
  bool dynamicGain = (voiceRegion->modLfoToVolume != 0);
  float sampleRate = getOutSampleRate();
  bool dynamicLowpass = (voiceRegion->modLfoToFilterFc || voiceRegion->modEnvToFilterFc);
  bool dynamicPitchRatio = (voiceRegion->modLfoToPitch || voiceRegion->modEnvToPitch || voiceRegion->vibLfoToPitch);
  
  float tmpInitialFilterFc = 0, tmpModLfoToFilterFc = 0, tmpModEnvToFilterFc = 0;
  if (dynamicLowpass) {
    tmpInitialFilterFc = (float)voiceRegion->initialFilterFc;
    tmpModLfoToFilterFc = (float)voiceRegion->modLfoToFilterFc;
    tmpModEnvToFilterFc = (float)voiceRegion->modEnvToFilterFc;
  }

  float tmpModLfoToPitch = 0.0f, tmpVibLfoToPitch = 0.0f, tmpModEnvToPitch = 0.0f;
  double pitchRatio = 0.0;
  if (dynamicPitchRatio) {
    tmpModLfoToPitch = (float)voiceRegion->modLfoToPitch;
    tmpVibLfoToPitch = (float)voiceRegion->vibLfoToPitch;
    tmpModEnvToPitch = (float)voiceRegion->modEnvToPitch;
  } else {
    pitchRatio = tsf_timecents2Secsd(pitchInputTimecents) * pitchOutputFactor;
  }

  float noteGain = 0.0f, tmpModLfoToVolume = 0.0f;
  if (dynamicGain) {
    tmpModLfoToVolume = (float)voiceRegion->modLfoToVolume * 0.1f;
  } else {
    noteGain = decibelsToGain(getGainDB());
  }
  
  while (numSamples) {
    int blockSamples = (numSamples > TSF_RENDER_EFFECTSAMPLEBLOCK ? TSF_RENDER_EFFECTSAMPLEBLOCK : numSamples);
    numSamples -= blockSamples;

    if (dynamicLowpass) {
      float fres = tmpInitialFilterFc + modlfo.getLevel() * tmpModLfoToFilterFc + modenv.getLevel() * tmpModEnvToFilterFc;
      float lowpassFc = (fres <= 13500 ? tsf_cents2Hertz(fres) / sampleRate : 1.0f);
      lowpass.active = (lowpassFc < 0.499f);
      if (lowpass.active) lowpass.setup(lowpassFc);
    }

    if (dynamicPitchRatio) {
      pitchRatio = tsf_timecents2Secsd(pitchInputTimecents + (modlfo.getLevel() * tmpModLfoToPitch + viblfo.getLevel() * tmpVibLfoToPitch + modenv.getLevel() * tmpModEnvToPitch)) * pitchOutputFactor;
    }

    if (dynamicGain) {
      noteGain = decibelsToGain(getGainDB() + (modlfo.getLevel() * tmpModLfoToVolume));
    }

    float gainMono = noteGain * ampenv.getLevel();
    
    // Update EG.
    ampenv.process(blockSamples);
    if (updateModEnv) modenv.process(blockSamples);
    
    // Update LFOs.
    if (updateModLFO) modlfo.process(blockSamples);
    if (updateVibLFO) viblfo.process(blockSamples);
                
    while (blockSamples-- && sourceSamplePosition < sampleEndDbl) {
      unsigned int pos = (unsigned int)sourceSamplePosition, nextPos = (pos >= loopEnd && isLooping ? loopStart : pos + 1);
      
      // Simple linear interpolation.
      float alpha = (float)(sourceSamplePosition - pos);
      float val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);
      
      // Low-pass filter.
      if (lowpass.active) val = lowpass.process(val);
      
      *output++ += val * gainMono;
	
      // Next sample.
      sourceSamplePosition += pitchRatio;
      if (sourceSamplePosition >= loopEndDbl && isLooping) sourceSamplePosition -= (loopEnd - loopStart + 1.0);
    }
    
    if (sourceSamplePosition >= sampleEndDbl || ampenv.isDone()) {
      killNote();
      break;
    }
  }

  // applyEffects(outputData);

  return outputData;
}

void tsf_load(SoundFontFile* res, struct tsf_stream* stream) {  
  struct tsf_riffchunk chunkHead;
  struct tsf_riffchunk chunkList;
  struct tsf_hydra hydra;
  float * fontSamples = nullptr;
  unsigned int fontSampleCount = 0;
  
  if (!tsf_riffchunk_read(nullptr, &chunkHead, stream) || !(chunkHead.id == "sfbk")) {
    //if (e) *e = TSF_INVALID_NOSF2HEADER;
    return;
  }
  
  // Read hydra and locate sample data.
  memset(&hydra, 0, sizeof(hydra));
  while (tsf_riffchunk_read(&chunkHead, &chunkList, stream)) {
    struct tsf_riffchunk chunk;
    if (chunkList.id == "pdta")
      {
	while (tsf_riffchunk_read(&chunkList, &chunk, stream))
	  {
#define HandleChunk(chunkName) (chunk.id == #chunkName && (chunk.size % chunkName##SizeInFile) == 0) \
	      {								\
		int num = chunk.size / chunkName##SizeInFile, i;	\
		hydra.chunkName##Num = num;				\
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
    else if (chunkList.id == "sdta")
      {
	while (tsf_riffchunk_read(&chunkList, &chunk, stream))
	  {
	    if (chunk.id == "smpl")
	      {
		tsf_load_samples(&fontSamples, &fontSampleCount, &chunk, stream);
	      }
	    else stream->skip(stream->data, chunk.size);
	  }
      }
    else stream->skip(stream->data, chunkList.size);
  }
  
  if (!hydra.phdrs || !hydra.pbags || !hydra.pmods || !hydra.pgens || !hydra.insts || !hydra.ibags || !hydra.imods || !hydra.igens || !hydra.shdrs) {
    //if (e) *e = TSF_INVALID_INCOMPLETE;
  } else if (fontSamples == nullptr) {
    //if (e) *e = TSF_INVALID_NOSAMPLEDATA;
  } else {
    size_t presetNum = hydra.phdrNum - 1;
    // res->presets = (struct tsf_preset*)malloc(res->presetNum * sizeof(struct tsf_preset));
    res->presets.resize(presetNum);
    res->fontSamples = fontSamples;
    fontSamples = nullptr; //don't free below
    tsf_load_presets(res, &hydra, fontSampleCount);
  }
  free(hydra.phdrs); free(hydra.pbags); free(hydra.pmods);
  free(hydra.pgens); free(hydra.insts); free(hydra.ibags);
  free(hydra.imods); free(hydra.igens); free(hydra.shdrs);
  free(fontSamples);
}

#if 0
static void tsf_channel_setup_voice(SoundFontFile* f, SoundFontVoice * v) {
  struct tsf_channel* c = &f->channels->channels[f->channels->activeChannel];
  v->playingChannel = f->channels->activeChannel;
  v->noteGainDB += c->gainDB;
  v->calcPitchRatio((c->pitchWheel == 8192 ? c->tuning : ((c->pitchWheel / 16383.0f * c->pitchRange * 2.0f) - c->pitchRange + c->tuning)));
}

static void tsf_channel_applypitch(SoundFontFile* f, int channel, struct tsf_channel* c)
{
	struct tsf_voice *v, *vEnd;
	float pitchShift = (c->pitchWheel == 8192 ? c->tuning : ((c->pitchWheel / 16383.0f * c->pitchRange * 2.0f) - c->pitchRange + c->tuning));
	for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
		if (v->playingChannel == channel && v->playingPreset != -1)
			v->calcPitchRatio(pitchShift);
}

void tsf_channel_set_pitchwheel(SoundFontFile* f, int channel, int pitch_wheel)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (c->pitchWheel == pitch_wheel) return;
	c->pitchWheel = (unsigned short)pitch_wheel;
	tsf_channel_applypitch(f, channel, c);
}

void tsf_channel_set_pitchrange(SoundFontFile* f, int channel, float pitch_range)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (c->pitchRange == pitch_range) return;
	c->pitchRange = pitch_range;
	if (c->pitchWheel != 8192) tsf_channel_applypitch(f, channel, c);
}

void tsf_channel_set_tuning(SoundFontFile* f, int channel, float tuning)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (c->tuning == tuning) return;
	c->tuning = tuning;
	tsf_channel_applypitch(f, channel, c);
}
#endif

void
SoundFont::openFile() {
  sf = make_shared<SoundFontFile>(filename);  
}  

class SoundFontInstrument : public Instrument {
public:
  SoundFontInstrument(std::shared_ptr<SoundFontFile> _sf, size_t _preset, size_t _fixedMidiKey) : Instrument(1), sf(_sf), preset(_preset), fixedMidiKey(_fixedMidiKey) { }
  
  std::unique_ptr<InstrumentVoice> createVoice(unsigned int outSampleRate, int identifier) const override {
    auto v = make_unique<SoundFontVoice>(outSampleRate, identifier, sf, preset, fixedMidiKey);
    v->createEffectStates(getEffects());
    return move(v);
  }

  void playNote(size_t column, float frequency, float velocity, float delay, float detune, VoicePool & voices) const override {
    voices.stopVoices(column);

    assert(frequency > 0);
    auto f = sf.get();

    if (preset >= f->presets.size()) return;

    double apparent_key = log2(frequency / 440) * 12 + 69;
    int midiKey = fixedMidiKey ? fixedMidiKey : int(apparent_key);
    
    short midiVelocity = (short)(velocity * 127);
    if (midiVelocity > 127) midiVelocity = 127;
  
    // Play all matching regions.

    auto & regions = f->presets[preset].regions;
    
    for (size_t region_idx = 0; region_idx < regions.size(); region_idx++) {
      auto & region = regions[region_idx];
      if (midiKey < region.lokey || midiKey > region.hikey || midiVelocity < region.lovel || midiVelocity > region.hivel) continue;

      if (region.group) {
	// FIXME: here we should end all voices with the same instrument and group
      }

      getVoice(column, voices).playNote(frequency, velocity, delay, detune, region_idx);
    }
  }
  
private:
  shared_ptr<SoundFontFile> sf;
  size_t preset;
  size_t fixedMidiKey = 0;
};

std::unique_ptr<Instrument>
SoundFont::createInstrument(size_t preset, size_t fixedMidiKey) {
  auto instrument = make_unique<SoundFontInstrument>(sf, preset, fixedMidiKey);
  instrument->setName(sf->getPresetName(preset));
  return instrument;
}

std::vector<std::unique_ptr<Instrument> >
SoundFont::createAll() {
  std::vector<std::unique_ptr<Instrument> > r;
  size_t n = sf->getPresetCount();
  for (size_t i = 0; i < n; i++) r.push_back(createInstrument(i));
  return r;
}
