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

#include "dsp/Biquad.h"
#include "FourCC.h"
#include "EnvelopeState.h"
#include "LFOState.h"
#include "InstrumentVoice.h"
#include "SendLevels.h"
#include "AudioBuffer.h"
#include "dsp/PanLaw.h"
#include "dsp/ChorusEngine.h"
#include "AmbisonicEncoding.h"
#include "SF2Modulator.h"
#include "dsp/NoiseGenerator.h"

#include "constants.h"

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
  float pan;
  Envelope ampenv, modenv;
  int initialFilterQ, initialFilterFc;
  int modEnvToPitch, modEnvToFilterFc, modLfoToFilterFc, modLfoToVolume;
  float delayModLFO;
  int freqModLFO, modLfoToPitch;
  float delayVibLFO;
  int freqVibLFO, vibLfoToPitch;
  float reverbEffectsSend, chorusEffectsSend;
  std::vector<SF2Mod::Connection> modulators;

  void clear(bool for_relative) {
    modulators.clear();
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
    reverbEffectsSend = 0;
    chorusEffectsSend = 0;

    ampenv.delay_ = ampenv.attack_ = ampenv.hold_ = ampenv.decay_ = ampenv.release_ = 0;
    modenv.delay_ = modenv.attack_ = modenv.hold_ = modenv.decay_ = modenv.release_ = 0;

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
    ampenv.delay_ = ampenv.attack_ = ampenv.hold_ = ampenv.decay_ = ampenv.release_ = -12000.0f;
    modenv.delay_ = modenv.attack_ = modenv.hold_ = modenv.decay_ = modenv.release_ = -12000.0f;
    
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

#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include <array>
#include <memory>

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

// Reads modulator records [begin, end) out of a pmod/imod hydra array into
// SF2Mod's own file-format-agnostic Connection shape - the modulator
// equivalent of the pgen/igen generator-walking loops in tsf_load_presets()
// below, just flattened into a vector up front instead of being consumed
// generator-by-generator.
template<typename ModArray>
static std::vector<SF2Mod::Connection> tsf_read_mods(ModArray* mods, uint16_t begin, uint16_t end) {
  std::vector<SF2Mod::Connection> result;
  if (end <= begin) return result;
  result.reserve(static_cast<size_t>(end - begin));
  for (uint16_t i = begin; i != end; i++) {
    SF2Mod::Connection c;
    c.src = mods[i].modSrcOper;
    c.dest = mods[i].modDestOper;
    c.amount = mods[i].modAmount;
    c.amtSrc = mods[i].modAmtSrcOper;
    c.trans = mods[i].modTransOper;
    result.push_back(c);
  }
  return result;
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
		{ GEN_FLOAT | GEN_FLOAT_MAX1000    , _TSFREGIONOFFSET(       float, chorusEffectsSend    ) }, //15 ChorusEffectsSend
		{ GEN_FLOAT | GEN_FLOAT_MAX1000    , _TSFREGIONOFFSET(       float, reverbEffectsSend    ) }, //16 ReverbEffectsSend
		{ GEN_FLOAT | GEN_FLOAT_LIMITPAN   , _TSFREGIONOFFSET(       float, pan                  ) }, //17 Pan
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //   Unused
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONOFFSET(       float, delayModLFO          ) }, //21 DelayModLFO
		{ GEN_INT   | GEN_INT_LIMIT16K4500 , _TSFREGIONOFFSET(         int, freqModLFO           ) }, //22 FreqModLFO
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONOFFSET(       float, delayVibLFO          ) }, //23 DelayVibLFO
		{ GEN_INT   | GEN_INT_LIMIT16K4500 , _TSFREGIONOFFSET(         int, freqVibLFO           ) }, //24 FreqVibLFO
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, modenv, delay_       ) }, //25 DelayModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, attack_      ) }, //26 AttackModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, modenv, hold_        ) }, //27 HoldModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, decay_       ) }, //28 DecayModEnv
		{ GEN_FLOAT | GEN_FLOAT_MAX1000    , _TSFREGIONENVOFFSET(    float, modenv, sustain_     ) }, //29 SustainModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, release_     ) }, //30 ReleaseModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, modenv, keynumToHold_) }, //31 KeynumToModEnvHold
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, modenv, keynumToDecay_) }, //32 KeynumToModEnvDecay
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, ampenv, delay_       ) }, //33 DelayVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, attack_      ) }, //34 AttackVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, ampenv, hold_        ) }, //35 HoldVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, decay_       ) }, //36 DecayVolEnv
		{ GEN_FLOAT | GEN_FLOAT_MAX1440    , _TSFREGIONENVOFFSET(    float, ampenv, sustain_     ) }, //37 SustainVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, release_     ) }, //38 ReleaseVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, ampenv, keynumToHold_) }, //39 KeynumToVolEnvHold
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, ampenv, keynumToDecay_) }, //40 KeynumToVolEnvDecay
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
  p->delay_   = (p->delay_  < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->delay_));
  p->attack_  = (p->attack_  < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->attack_));
  p->release_ = (p->release_ < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->release_));
  p->hold_  = (p->hold_ < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->hold_));
  p->decay_ = (p->decay_ < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->decay_));
    
  if (p->sustain_ < 0.0f) p->sustain_ = 0.0f;
  else if (sustainIsGain) p->sustain_ = TrackState::decibelsToGain(-p->sustain_ / 10.0f);
  else p->sustain_ = 1.0f - (p->sustain_ / 1000.0f);
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
  SoundFontFile(const std::string & filename) {
    loadFile(filename);
  }
  ~SoundFontFile() {
    free(fontSamples_);
  }

  // Directly load a SoundFont from a .sf2 file path
  void loadFile(const std::string & filename) {
    struct tsf_stream stream = { nullptr, (int(*)(void*,void*,unsigned int))&tsf_stream_stdio_read, (int(*)(void*,unsigned int))&tsf_stream_stdio_skip };
#if __STDC_WANT_SECURE_LIB__
    FILE * fh = nullptr;
    fopen_s(&fh, filename.c_str(), "rb");
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
  const char * getPresetName(size_t index) const {
    if (index < presets_.size()) return presets_[index].presetName;
    else return "";
  }

  // Returns the preset index from a bank and preset number, or -1 if it does not exist in the loaded SoundFont

  int getPresetIndex(int bank, int preset_number) const {
    for (size_t i = 0; i < presets_.size(); i++) {
      if (presets_[i].preset == preset_number && presets_[i].bank == bank) {
	return i;
      }
    }
    return -1;
  }

  size_t getPresetCount() const { return presets_.size(); }
  
  std::vector<tsf_preset> presets_;
  float * fontSamples_ = nullptr;
};

// Experimental, out-of-spec GM-family channel-pressure heuristic defaults -
// see CMakeLists.txt's own comment on SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS
// for why this is opt-in (off by default). Guarded with a plain #ifdef,
// not a runtime flag, so the whole thing - table, injection logic, and
// its own tests in tests/SF2ModulatorTests.cpp - can be deleted outright
// later with no dangling references anywhere, not just switched off.
#ifdef SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS

namespace {

  enum class GmPressureDefault : uint8_t { Disabled, Vibrato, FilterCutoff };

  // GM program (0-127) -> heuristic channel-pressure default. Assignment
  // follows physical playing mechanism (a bow/breath/bellows can add real
  // performer vibrato, or brighten under pressure the way blowing harder
  // does; a struck/plucked/keyboard instrument can't), not GM's own
  // family groupings verbatim - GM's "Organ" (16-23) and "Ethnic"
  // (104-111) ranges each straddle more than one physical instrument
  // type, so those two ranges split mid-family below.
  const GmPressureDefault kGmPressureDefault[128] = {
    // 0-7 Piano
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    // 8-15 Chromatic Percussion
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    // 16-20 Organ, genuine keyboard/pipe (Drawbar/Percussive/Rock/Church/Reed Organ) - gate-exempt, see isKeyboardPipeOrgan below
    GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato,
    // 21-23 Organ per GM but physically reed (Accordion/Harmonica/Bandoneon) - gate applies, same as strings/voice
    GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato,
    // 24-31 Guitar
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    // 32-39 Bass
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    // 40-44 Strings, bowed (Violin/Viola/Cello/Contrabass/Tremolo Strings)
    GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato,
    // 45-47 Strings, not bowed (Pizzicato Strings/Orchestral Harp/Timpani)
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    // 48-55 Ensemble/Voice
    GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato,
    GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato,
    // 56-63 Brass
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    // 64-71 Reed
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    // 72-79 Pipe
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    // 80-87 Synth Lead
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    // 88-95 Synth Pad
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff, GmPressureDefault::FilterCutoff,
    // 96-103 Synth Effects
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    // 104-108 Ethnic, plucked/struck (Sitar/Banjo/Shamisen/Koto/Kalimba)
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    // 109-111 Ethnic, breath/bowed (Bag pipe/Fiddle/Shanai)
    GmPressureDefault::Vibrato, GmPressureDefault::Vibrato, GmPressureDefault::Vibrato,
    // 112-119 Percussive
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    // 120-127 Sound Effects
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
    GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled, GmPressureDefault::Disabled,
  };

  constexpr int16_t kHeuristicVibratoAmount = 10; // cents - matches TimGM6mb.sf2's own authored channel-pressure-vibrato amount
  constexpr int16_t kHeuristicFilterCutoffAmount = 2400; // cents (~2 octaves at full pressure) - no real authored file to calibrate against, a tunable starting point
  constexpr int kFilterCutoffGateThreshold = 13000; // cents - spec default is 13500 ("wide open"); only brighten a filter the patch already configured

  // True for GM 16-20 (Drawbar/Percussive/Rock/Church/Reed Organ) - genuine
  // keyboard/pipe organs have no embouchure/bow/breath at all, so unlike
  // every other vibrato-eligible family there's no baked-in-sample-vibrato
  // risk to gate against (see the design doc this heuristic was planned
  // from). GM 21-23 (Accordion/Harmonica/Bandoneon) share GM's "Organ"
  // grouping but are physically breath/bellows-driven reed instruments,
  // so they keep the gate like strings/voice do.
  bool isKeyboardPipeOrgan(uint16_t gmProgram) { return gmProgram >= 16 && gmProgram <= 20; }

  // Decides whether `zoneRegion` (a bank-0/GM preset's fully generator-
  // merged region for GM program `gmProgram`) should get a heuristic
  // channel-pressure modulator injected - the file itself must not already
  // manage channel pressure (checked by the caller via
  // SF2Mod::isChannelPressureSourced before calling this), and, other than
  // the keyboard/pipe-organ exemption above, the destination generator
  // must already deviate from its SF2 spec default (i.e. the patch already
  // opted into that modulation mechanism via its own static generators).
  bool heuristicChannelPressureModulator(uint16_t gmProgram, const tsf_region & zoneRegion, SF2Mod::Connection * outConnection) {
    if (gmProgram >= 128) return false;
    GmPressureDefault def = kGmPressureDefault[gmProgram];
    if (def == GmPressureDefault::Disabled) return false;

    outConnection->src = static_cast<uint16_t>(SF2Mod::GeneralController::ChannelPressure);
    outConnection->amtSrc = static_cast<uint16_t>(SF2Mod::GeneralController::NoController);
    outConnection->trans = 0;

    if (def == GmPressureDefault::Vibrato) {
      if (!isKeyboardPipeOrgan(gmProgram) && zoneRegion.vibLfoToPitch == 0) return false;
      outConnection->dest = 6; // VibLfoToPitch
      outConnection->amount = kHeuristicVibratoAmount;
      return true;
    }

    // FilterCutoff
    if (zoneRegion.initialFilterFc >= kFilterCutoffGateThreshold) return false;
    outConnection->dest = 8; // InitialFilterFc
    outConnection->amount = kHeuristicFilterCutoffAmount;
    return true;
  }

}

#endif // SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS

static void tsf_load_presets(SoundFontFile* res, struct tsf_hydra *hydra, unsigned int fontSampleCount) {
  enum { GenInstrument = 41, GenKeyRange = 43, GenVelRange = 44, GenSampleID = 53 };
  // Read each preset.
  struct tsf_hydra_phdr *pphdr, *pphdrMax;
  for (pphdr = hydra->phdrs, pphdrMax = pphdr + hydra->phdrNum - 1; pphdr != pphdrMax; pphdr++) {
    size_t sortedIndex = 0, region_index = 0;
    struct tsf_hydra_phdr *otherphdr;
    struct tsf_hydra_pbag *ppbag, *ppbagEnd;
    struct tsf_region globalRegion;
    for (otherphdr = hydra->phdrs; otherphdr != pphdrMax; otherphdr++) {
      if (otherphdr == pphdr || otherphdr->bank > pphdr->bank) continue;
      else if (otherphdr->bank < pphdr->bank) sortedIndex++;
      else if (otherphdr->preset > pphdr->preset) continue;
      else if (otherphdr->preset < pphdr->preset) sortedIndex++;
      else if (otherphdr < pphdr) sortedIndex++;
    }
    
    auto preset = &(res->presets_[sortedIndex]);
    
    memcpy(preset->presetName, pphdr->presetName, sizeof(preset->presetName));
    preset->presetName[sizeof(preset->presetName)-1] = '\0'; //should be zero terminated in source file but make sure
    preset->bank = pphdr->bank;
    preset->preset = pphdr->preset;
    
    size_t regionNum = 0;
    
    // count regions covered by this preset
    for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++) {
      unsigned char plokey = 0, phikey = 127, plovel = 0, phivel = 127;
      struct tsf_hydra_pgen *ppgen, *ppgenEnd;
      struct tsf_hydra_ibag *pibag, *pibagEnd;
      struct tsf_hydra_igen *pigen, *pigenEnd;
      
      for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++) {
	if (ppgen->genOper == GenKeyRange) {
	  plokey = ppgen->genAmount.range.lo;
	  phikey = ppgen->genAmount.range.hi;
	  continue;
	}
	if (ppgen->genOper == GenVelRange) {
	  plovel = ppgen->genAmount.range.lo;
	  phivel = ppgen->genAmount.range.hi;
	  continue;
	}
	if (ppgen->genOper != GenInstrument) continue;
	if (ppgen->genAmount.wordAmount >= hydra->instNum) continue;
	
	struct tsf_hydra_inst * pinst = hydra->insts + ppgen->genAmount.wordAmount;
	for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++) {
	  unsigned char ilokey = 0, ihikey = 127, ilovel = 0, ihivel = 127;
	  for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++) {
	    if (pigen->genOper == GenKeyRange) {
	      ilokey = pigen->genAmount.range.lo;
	      ihikey = pigen->genAmount.range.hi;
	      continue;
	    }
	    if (pigen->genOper == GenVelRange) {
	      ilovel = pigen->genAmount.range.lo;
	      ihivel = pigen->genAmount.range.hi;
	      continue;
	    }
	    if (pigen->genOper == GenSampleID && ihikey >= plokey && ilokey <= phikey && ihivel >= plovel && ilovel <= phivel) regionNum++;
	  }
	}
      }
    }
    
    preset->regions.resize(regionNum);
    globalRegion.clear(true);
    
    // Zones.
    for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++) {
      struct tsf_hydra_pgen *ppgen, *ppgenEnd;
      struct tsf_hydra_inst *pinst;
      struct tsf_hydra_ibag *pibag, *pibagEnd;
      struct tsf_hydra_igen *pigen, *pigenEnd;
      struct tsf_region presetRegion = globalRegion;
      // This preset zone's own modulators override any matching (same
      // src/dest/amtSrc/trans identity) modulator inherited from the
      // preset's global zone (globalRegion.modulators, SF2.01 7.4) -
      // presetRegion.modulators currently holds exactly that inherited set
      // (copied above), so it's the base and this zone's own reads are the
      // override.
      presetRegion.modulators = mergeModulators(presetRegion.modulators, tsf_read_mods(hydra->pmods, ppbag->modNdx, ppbag[1].modNdx));

      int hadGenInstrument = 0;
      
      // Generators.
      for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++)	{
	// Instrument.
	if (ppgen->genOper == GenInstrument) {
	  struct tsf_region instRegion;
	  uint16_t whichInst = ppgen->genAmount.wordAmount;
	  if (whichInst >= hydra->instNum) continue;
	  
	  instRegion.clear(false);
	  pinst = &hydra->insts[whichInst];
	  for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++) {
	    // Generators.
	    struct tsf_region zoneRegion = instRegion;
	    // Same override-over-inherited-global-zone shape as presetRegion's
	    // modulators above, one level down (SF2.01 7.4's instrument-level
	    // global zone).
	    zoneRegion.modulators = mergeModulators(zoneRegion.modulators, tsf_read_mods(hydra->imods, pibag->instModNdx, pibag[1].instModNdx));
	    int hadSampleID = 0;
	    for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++) {
	      if (pigen->genOper == GenSampleID) {
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

		// SF2.01 9.5.1: an "identical" preset-level modulator (same
		// src/dest/amtSrc/trans identity) replaces the instrument-level
		// one entirely, rather than combining like generators do -
		// zoneRegion.modulators is the instrument-level base,
		// presetRegion.modulators (already merged against the preset's
		// own global zone above) is the override.
		zoneRegion.modulators = mergeModulators(zoneRegion.modulators, presetRegion.modulators);

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

#ifdef SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS
		// GM-family heuristic default (see heuristicChannelPressureModulator
		// above): only for melodic GM presets whose own modulators don't
		// already manage channel pressure at all - a file that authors even
		// one channel-pressure connection (to any destination) is trusted
		// exactly as written, never supplemented.
		if (pphdr->bank == 0) {
		  bool alreadyManaged = false;
		  for (const auto & c : zoneRegion.modulators) {
		    if (SF2Mod::isChannelPressureSourced(c)) { alreadyManaged = true; break; }
		  }
		  if (!alreadyManaged) {
		    SF2Mod::Connection heuristic;
		    if (heuristicChannelPressureModulator(pphdr->preset, zoneRegion, &heuristic)) {
		      zoneRegion.modulators = mergeModulators(zoneRegion.modulators, { heuristic });
		    }
		  }
		}
#endif // SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS

		preset->regions[region_index] = zoneRegion;
		region_index++;
		hadSampleID = 1;
	      } else {
		tsf_region_operator(&zoneRegion, pigen->genOper, &pigen->genAmount, nullptr);
	      }
	    }
	    
	    // Handle instrument's global zone.
	    if (pibag == hydra->ibags + pinst->instBagNdx && !hadSampleID) {
	      instRegion = zoneRegion;
	    }
	  }
	  hadGenInstrument = 1;
	} else {
	  tsf_region_operator(&presetRegion, ppgen->genOper, &ppgen->genAmount, nullptr);
	}
      }

      // Handle preset's global zone.
      if (ppbag == hydra->pbags + pphdr->presetBagNdx && !hadGenInstrument) {
	globalRegion = presetRegion;
      }
    }
  }
}

// The region's own pan/send-generator data (voiceRegion_) is only known
// once we've looked up preset/region_idx - which happens inside the
// constructor body, after the InstrumentVoice base has already been
// constructed. But that data is fixed for the voice's whole lifetime (an
// SF2 region never changes once a voice is created), so rather than
// re-deriving the combined position/sends on every getPosition()/getSendA()/
// getSendB() call via a virtual override, look the region up once more
// (cheap - just two array-bounds checks) directly in the constructor's own
// initializer list and bake the already-combined values straight into the
// base class's stored position_/send_a_/send_b_, exactly like every other
// leaf voice does.
static const tsf_region *
regionFor(const SoundFontFile * f, size_t preset, size_t region_idx) {
  if (preset >= f->presets_.size()) return nullptr;
  auto & regions = f->presets_[preset].regions;
  if (region_idx >= regions.size()) return nullptr;
  return &regions[region_idx];
}

// Folds the SF2 region's own pan into an azimuth offset: find the azimuth
// delta whose sine matches the desired pan offset (asin is the inverse of
// the sin() inside PanLaw.h's azimuthToPan(), the same convention) and add
// it to the voice's own azimuth. This is the only way an SF2 instrument's
// authored stereo image (velocity layers/stereo-split regions panned
// differently) reaches the mix, since this voice only ever renders a
// single dry signal of its own - encodePosition() (InstrumentVoice.h)
// spatially encodes it using exactly this (already-adjusted) position.
// Deliberately does NOT force a real distance when the track itself never
// set one: computeAmbisonicGains() treats distance <= 0 as "no position at
// all" (W-only, diffuse) regardless of azimuth, so an untouched track with
// no configured position stays diffuse - the region's pan simply has no
// audible effect there, rather than inventing a distance to make it
// "work" for a track that never asked to be spatially positioned.
//
// The region's pan describes this specific instrument's own stereo width
// (e.g. a piano recorded with separately-panned left/right mic zones) -
// like any real spatially-extended source, that width should narrow with
// distance rather than staying constant forever, or a piano heard from
// 100 units away would sound just as wide as one right next to the
// listener. Reuses the exact same 1/distance law as
// InstrumentVoice::getDistanceGain() (not a new falloff curve), capped at
// 1.0 so it only ever narrows the authored width, never exaggerates it
// for a track placed closer than distance 1 - full width at distance <= 1
// ("near"), and by distance 100 only 1% of it survives (far enough to
// read as a single point source).
static SphericalPosition
adjustPositionForPan(const SphericalPosition & position, const tsf_region * region) {
  if (!region) return position;
  float pan_offset = region->pan - 0.5f;
  if (pan_offset < -0.5f) pan_offset = -0.5f;
  else if (pan_offset > 0.5f) pan_offset = 0.5f;
  auto adjusted = position;
  if (pan_offset != 0.0f) {
    float width_scale = std::min(1.0f, distanceGain(position.distance));
    adjusted.azimuth += asinf(pan_offset * 2.0f) * 180.0f / static_cast<float>(M_PI) * width_scale;
  }
  return adjusted;
}

// Combines the track's own user-configured SendA knob with this region's
// SF2 reverbEffectsSend generator data (parsed in tenths of a percent,
// 0-1000) - additive-then-clamped, mirroring SF2's own existing
// generator-merge convention (region + preset offsets, summed then
// clamped) rather than inventing a new combination rule. SendA still
// means exactly what it always has (the shared reverb bus), so this
// combination is unaffected by anything below. Named for the region side
// of the merge specifically (not just "adjustSendA") so it can't be
// confused with - or shadow via unqualified lookup in this constructor's
// own initializer list - TrackState::adjustSendA()/InstrumentVoice::
// adjustSendA(), the unrelated live-voice Send A push (TrackState.h).
static float
combineRegionSendA(float send_a, const tsf_region * region) {
  return std::min(1.0f, send_a + (region ? region->reverbEffectsSend / 1000.0f : 0.0f));
}

// Deliberately NOT combined with the track's own SendB knob (unlike
// combineRegionSendA above): SendB is now the shared multi-tap delay bus (see
// bus/MultiTapDelay.h), not chorus - a region's chorusEffectsSend hint has
// nothing to do with "how much of this voice should reach the delay bus",
// so folding it into send_b would silently and unintentionally louden a
// SoundFont voice's delay send whenever its patch happens to want chorus.
// Instead this drives only the voice's own per-voice chorus (see
// SoundFontVoice::chorus_engine_) - completely separate signal paths that
// happen to share the same SF2 generator as their trigger.
static float
chorusSendFor(const tsf_region * region) {
  return region ? region->chorusEffectsSend / 1000.0f : 0.0f;
}

// Which generator categories a channel-pressure modulator connection can
// usefully drive - the only two SoundFontVoice::render() actually
// recomputes every block (see dynamicLowpass/dynamicPitchRatio there). A
// modulator targeting anything else parses correctly (SF2Mod::Connection
// doesn't restrict dest at all) but is a documented no-op here, the same
// scope limitation the original poly-pressure plan already accepted.
static bool destInFilterCutoffSet(uint16_t dest) {
  return dest == 8 || dest == 10 || dest == 11; // InitialFilterFc, ModLfoToFilterFc, ModEnvToFilterFc
}
static bool destInPitchSet(uint16_t dest) {
  return dest == 5 || dest == 6 || dest == 7 || dest == 51 || dest == 52; // ModLfoToPitch, VibLfoToPitch, ModEnvToPitch, CoarseTune, FineTune
}

class SoundFontVoice : public InstrumentVoice {
public:
  // skip_native_pan: TEMPORARY - true only for GM percussion (bank 128)
  // regions, whose position is already fully resolved by
  // applyPercussionOffset() (SoundFontInstrument::playNote()). The
  // region's own native SF2 pan (adjustPositionForPan(), below) uses a
  // pan-convention that's under separate investigation, so for now,
  // percussion plays from exactly the new key-offset-resolved position
  // instead of also folding in that region pan. Every other instrument
  // (pitched SF2 presets, non-percussion banks) is untouched by any of
  // this and keeps using adjustPositionForPan() exactly as before -
  // defaults to false so every other call site is unaffected.
  SoundFontVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, float detune, float start_phase, std::shared_ptr<SoundFontFile> sf, size_t preset, size_t region_idx, const SendLevels & sends = {}, bool skip_native_pan = false)
    : InstrumentVoice(channel_config,
                       skip_native_pan ? position : adjustPositionForPan(position, regionFor(sf.get(), preset, region_idx)),
                       detune, start_phase,
                       SendLevels{ sends.main, combineRegionSendA(sends.a, regionFor(sf.get(), preset, region_idx)), sends.b }),
      sf_(sf)
  {
    auto f = sf_.get();
    if (preset < f->presets_.size()) {
      auto & regions = f->presets_[preset].regions;
      assert(region_idx < regions.size());
      if (region_idx < regions.size()) {
	auto & region = regions[region_idx];
	voiceRegion_ = &region;

	// Precompute once (rather than rescanning every block in render())
	// whether this region has any channel-pressure-sourced modulator
	// targeting a category render() actually evaluates - see
	// destInFilterCutoffSet/destInPitchSet above.
	for (auto & c : voiceRegion_->modulators) {
	  if (!SF2Mod::isChannelPressureSourced(c)) continue;
	  if (destInFilterCutoffSet(c.dest)) hasChannelPressureCutoffMod_ = true;
	  if (destInPitchSet(c.dest)) hasChannelPressurePitchMod_ = true;
	}

	// Offset/end (add to the start phase)
	// sourceSamplePosition_ += voiceRegion_->offset;
	sourceSamplePosition_ = voiceRegion_->offset;

	auto outSampleRate = getChannelConfiguration().getAudioOutSampleRate();

	// Loop.
	bool doLoop = (voiceRegion_->loop_mode != TSF_LOOPMODE_NONE && voiceRegion_->loop_start < voiceRegion_->loop_end);
	loopStart_ = (doLoop ? voiceRegion_->loop_start : 0);
	loopEnd_ = (doLoop ? voiceRegion_->loop_end : 0);

	// Setup LFO filters.
	modlfo_ = LFOState(voiceRegion_->delayModLFO, tsf_cents2Hertz(voiceRegion_->freqModLFO), outSampleRate);
	viblfo_ = LFOState(voiceRegion_->delayVibLFO, tsf_cents2Hertz(voiceRegion_->freqVibLFO), outSampleRate);	
	
	// Setup lowpass filter.
	float lowpassFc = (voiceRegion_->initialFilterFc <= 13500 ? tsf_cents2Hertz((float)voiceRegion_->initialFilterFc) / outSampleRate : 1.0f);
	
	lowpass_.active_ = (lowpassFc < 0.499f);
	if (lowpass_.active_) {
	  float lowpassFilterQDB = voiceRegion_->initialFilterQ / 10.0f;
	  lowpass_.set(lowpassFc, pow(10.0, (lowpassFilterQDB / 20.0)));
	}
      }
    }

    // Per-voice chorus (see chorus_engine_'s own comment below) -
    // allocated only when this region actually has a nonzero
    // chorusEffectsSend, so a voice with none pays nothing extra. Same
    // voices/rate/delay/depth as the per-track <chorus> effect's own
    // defaults (effects/Chorus.cpp) - no reason for this to sound like a
    // different chorus character. Deliberately keyed off chorusSendFor(),
    // not getSendB() - see chorusSendFor()'s own comment.
    chorus_send_ = chorusSendFor(voiceRegion_);
    if (chorus_send_ > 0.0f) {
      chorus_engine_ = make_unique<ChorusEngine>(2, getChannelConfiguration().getAudioOutSampleRate(), /*voices=*/3, /*rateHz=*/0.5f, /*centerDelayMs=*/15.0f, /*depthMs=*/4.0f, /*decorrelate=*/true);
      chorus_engine_->setMix(1.0f); // fully wet - chorus_send_ controls how much of it reaches the mix, applied at encode time
    }
  }
  
  void playNote(float frequency, float velocity, int note_value) override {
    assert(frequency > 0);
    if (!voiceRegion_) return;

    note_value_ = note_value;
    velocity_ = velocity;

    bool first_play = apparentPlayingKey_ == 0.0;
    
    apparentPlayingKey_ = log2(frequency * getDetune() / 440) * 12 + 69;

    if (first_play) {
      // use correctly rounded midiKey for envelopes (should we use detune?)
      auto midiKey = int(round(log2(frequency / 440) * 12 + 69));
      auto midiVelocity = (short)(velocity * 127);
      if (midiVelocity > 127) midiVelocity = 127;

      auto outSampleRate = getChannelConfiguration().getAudioOutSampleRate();

      // Setup envelopes.
      ampenv_ = EnvelopeState(outSampleRate, voiceRegion_->ampenv, midiKey, midiVelocity, true);
      modenv_ = EnvelopeState(outSampleRate, voiceRegion_->modenv, midiKey, midiVelocity, false);
    }
                  
    setGainDB(- voiceRegion_->attenuation - gainToDecibels(1.0f / velocity));
    calcPitchRatio(0);
  }

  bool isActive() const override {
    return voiceRegion_ && sourceSamplePosition_ < voiceRegion_->end && !ampenv_.isDone();
  }

  float getOwnLoudnessFactor() const override {
    return InstrumentVoice::getOwnLoudnessFactor() * ampenv_.getLevel();
  }

  // Track-broadcast channel pressure (see InstrumentTrackState::
  // broadcastChannelPressure()/applyRealChannelPressure()) - already
  // normalized to [0,1], same convention as applyAftertouch's own
  // argument. Only has any audible effect when hasChannelPressureCutoffMod_/
  // hasChannelPressurePitchMod_ was set at construction (see render()).
  // Still cascades to this voice's own children (FM modulators, see
  // SoundFontInstrument::playNote()) via the base class, matching
  // applyAftertouch's existing precedent, even though nothing among
  // today's modulator types reads it.
  void applyChannelPressure(float pressure) override {
    sf2_channel_pressure_ = pressure;
    TrackState::applyChannelPressure(pressure);
  }
  float getChannelPressure() const override { return sf2_channel_pressure_; }

  AudioBuffer render(int numSamples) override;
  
  void killNote() override {
    ampenv_.nextSegment(EnvelopeState::DONE);
    modenv_.nextSegment(EnvelopeState::DONE);

    // A modulator child attached by GenericInstrument::playNote() (a
    // song-configured FM modulator - see e.g. songs/subtractive_test.xml's
    // <genericInstrument name="Cello"><oscilator .../></genericInstrument>)
    // is stored directly in this voice's own children_ (TrackState::
    // addChild()). Without recursing here, that child never learns the
    // note stopped: its own envelope never reaches DONE, so it reports
    // itself active forever - getVoiceCount() (TrackState::getVoiceCount(),
    // not overridden here, so it does recurse into children) keeps
    // counting a "voice" that's already been silently orphaned once this
    // SoundFontVoice's own isActive() (which does NOT consult children)
    // goes false and clearFinishedVoices() reaps the whole subtree anyway.
    // Same recursion precedent as applyChannelPressure() below.
    TrackState::killNote();
  }

  void stopNote() override {
    // EnvelopeState::nextSegment(SUSTAIN) unconditionally forces segment
    // to RELEASE, regardless of the envelope's actual current segment -
    // calling it on an envelope that already reached DONE resurrects it
    // (isDone() starts reporting false again). stopNote() can legitimately
    // be called more than once on the same voice: InstrumentTrackState::
    // stopVoices() calls it both on a real note-off and again whenever a
    // later note-on retriggers a column still holding this voice, gated
    // only by the *group's* isActive() (TrackState::isActive() ORs over
    // children) - in a multi-region group (stereo/velocity-layered GM
    // patches, the common case) a sibling region can still be genuinely
    // active while this one already finished, so that guard alone doesn't
    // stop a second stopNote() from reaching an already-done region here.
    // Guarding per-envelope, not just at the call site, is what actually
    // prevents the revival.
    if (!ampenv_.isDone()) ampenv_.nextSegment(EnvelopeState::SUSTAIN);
    if (!modenv_.isDone()) modenv_.nextSegment(EnvelopeState::SUSTAIN);

    if (voiceRegion_->loop_mode == TSF_LOOPMODE_SUSTAIN) {
      // Continue playing, but stop looping.
      loopEnd_ = loopStart_;
    }

    // See killNote()'s comment above. Deliberately TrackState::killNote(),
    // not stopNote(): a modulator child's own envelope only ever advances
    // via process(), called exclusively from within SoundFontVoice::
    // render() on *its own* ampenv_/modenv_ - render() never recurses into
    // children_ (a modulator's rendered audio isn't consumed by anything
    // today), so a child put into RELEASE via stopNote() would sit there
    // forever, never reaching DONE. Jumping straight to DONE is the only
    // way this child ever reports itself inactive at all.
    TrackState::killNote();
  }

  // Reclaims this voice quickly without a hard cut - see TrackState::
  // fastRelease()'s own comment for when this is used (identity-based
  // retrigger cutoff, SF2 exclusive-class choking). Forcing release_ to 0
  // makes EnvelopeState::nextSegment(SUSTAIN) fall back to
  // TSF_FASTRELEASETIME (10ms) regardless of this region's own authored
  // release time - same envelope machinery a normal release already uses,
  // just compressed, never an abrupt amplitude jump. Same isDone() guard
  // as stopNote() above and for the same reason: this can be called on a
  // voice that's already DONE (e.g. already fast-released by one
  // mechanism, then matched by the other - see InstrumentTrackState::
  // chokeExclusiveClasses()'s own comment on why that's fine), and an
  // unconditional nextSegment(SUSTAIN) would resurrect it.
  void fastRelease() override {
    if (!ampenv_.isDone()) {
      ampenv_.parameters.release_ = 0.0f;
      ampenv_.nextSegment(EnvelopeState::SUSTAIN);
    }
    if (!modenv_.isDone()) {
      modenv_.parameters.release_ = 0.0f;
      modenv_.nextSegment(EnvelopeState::SUSTAIN);
    }

    if (voiceRegion_->loop_mode == TSF_LOOPMODE_SUSTAIN) {
      // Continue playing, but stop looping - same as stopNote() above.
      loopEnd_ = loopStart_;
    }

    // See killNote()'s comment above for why TrackState::killNote(), not
    // stopNote()/fastRelease(): a modulator child's envelope only ever
    // advances via process(), never called on children, so it must be
    // jumped straight to DONE rather than put into a RELEASE it can never
    // finish on its own.
    TrackState::killNote();
  }

  // Every distinct non-zero SF2 exclusive class (region.group) this
  // voice's own region belongs to - see TrackState::getExclusiveClasses()'s
  // own comment. A single SoundFontVoice always has exactly one region
  // (voiceRegion_), so this is at most one value; 0 means "no class",
  // same convention SoundFontInstrument::playNote() already uses.
  std::vector<int> getExclusiveClasses() const override {
    if (voiceRegion_ && voiceRegion_->group != 0) return { static_cast<int>(voiceRegion_->group) };
    return {};
  }

  void calcPitchRatio(float pitchShift) {
    auto note = apparentPlayingKey_ + voiceRegion_->transpose + voiceRegion_->tune / 100.0;
    auto adjustedPitch = voiceRegion_->pitch_keycenter + (note - voiceRegion_->pitch_keycenter) * (voiceRegion_->pitch_keytrack / 100.0);
    if (pitchShift) adjustedPitch += pitchShift;
    pitchInputTimecents_ = adjustedPitch * 100.0;
    pitchOutputFactor_ = voiceRegion_->sample_rate / (tsf_timecents2Secsd(voiceRegion_->pitch_keycenter * 100.0) * getChannelConfiguration().getAudioOutSampleRate());
  }

protected:
  double apparentPlayingKey_ = 0;
  struct tsf_region * voiceRegion_ = nullptr;
  double pitchInputTimecents_ = 0, pitchOutputFactor_ = 0;
  unsigned int loopStart_ = 0, loopEnd_ = 0;
  bool hasChannelPressureCutoffMod_ = false, hasChannelPressurePitchMod_ = false;
  float sf2_channel_pressure_ = 0.0f;
  Biquad<double> lowpass_ { FilterType::lowpass };
  LFOState modlfo_, viblfo_;
  
private:
  shared_ptr<SoundFontFile> sf_;
  EnvelopeState ampenv_, modenv_;
  vector<float> dry_;

  // Per-voice chorus realization of this region's chorusEffectsSend
  // generator (chorusSendFor() above) - SendB no longer hosts a bus-wide
  // chorus (see bus/MultiTapDelay.h, which replaced it), so an SF2 patch
  // that wants chorus character always gets its own instance instead,
  // completely independent of the track's own SendB knob (getSendB()) -
  // see chorusSendFor()'s own comment for why these two must never be
  // combined. Only allocated when chorus_send_ > 0 (see the constructor).
  // Fed dry_ duplicated into 2 identical channels, same as the old
  // ChorusBusEffect's approach - decorrelate=true diverges them into
  // stereo width purely from LFO phase offset, since a mono voice has no
  // pre-existing width of its own to preserve. The resulting 2 channels
  // are then each encoded as their own point source, offset a small
  // ~15-degree azimuth to either side of this voice's own (already
  // pan-adjusted) direction, narrowed by distance exactly like
  // adjustPositionForPan()'s own width scaling (a chorus voice 100 units
  // away should read as an unspread point source, same as its pan-derived
  // width does) - reusing the voice's own already distance-attenuated
  // dry_ as input, so no separate distance-attenuation handling is needed
  // here, only the width narrowing.
  unique_ptr<ChorusEngine> chorus_engine_;
  float chorus_send_ = 0.0f;
  array<AmbisonicVoiceEncoder, 2> chorus_tap_encoders_;
  AudioBuffer chorus_scratch_;

  // encodePosition() plus, when chorus_engine_ exists, this voice's own
  // per-voice chorus taps added on top (see chorus_engine_'s comment
  // above) - the single path both render() return points go through, so
  // an inactive voice's all-silent dry_ still gets a consistently-shaped
  // AudioBuffer (chorus of silence is silence, just extra unused work) and
  // an active voice's real dry_ gets both encoded together.
  AudioBuffer
  encodeWithChorus(int totalSamples) {
    auto data = encodePosition(dry_.data(), totalSamples);
    if (!chorus_engine_) return data;

    if (chorus_scratch_.numberOfFrames() != totalSamples) chorus_scratch_ = AudioBuffer(2, totalSamples);
    auto c0 = chorus_scratch_.getChannelData(0), c1 = chorus_scratch_.getChannelData(1);
    for (int i = 0; i < totalSamples; i++) c0[i] = c1[i] = dry_[static_cast<size_t>(i)];
    chorus_engine_->process(chorus_scratch_);
    auto wetL = chorus_scratch_.getChannelData(0), wetR = chorus_scratch_.getChannelData(1);

    auto pos = getPosition();
    float width_scale = std::min(1.0f, distanceGain(pos.distance));
    float offset = 15.0f * width_scale;
    auto leftGains = computeAmbisonicGains(SphericalPosition{ pos.azimuth - offset, pos.elevation, pos.distance });
    auto rightGains = computeAmbisonicGains(SphericalPosition{ pos.azimuth + offset, pos.elevation, pos.distance });
    // wetL/wetR come from dry_, which (like encodePosition()'s own `dry`
    // parameter) no longer carries distance attenuation - chorus_send_'s
    // own gain-scaling is where that has to be folded back in now, the
    // same "apply distance where the gains actually get used, not baked
    // into the sample buffer upstream" convention encodePosition() itself
    // uses for its main channels.
    // Only meaningful if data actually has Main channels to encode into -
    // encodePosition() above already skips allocating them entirely when
    // this voice's Send Main level is 0 (see its own doc comment).
    if (data.hasChannel(Channel::Main)) {
      float distance_gain = getDistanceGain();
      for (auto & g : leftGains) g *= chorus_send_ * distance_gain;
      for (auto & g : rightGains) g *= chorus_send_ * distance_gain;
      chorus_tap_encoders_[0].encodeBlock(data, wetL, totalSamples, leftGains);
      chorus_tap_encoders_[1].encodeBlock(data, wetR, totalSamples, rightGains);
    }

    // The track's own SendA/SendB knobs also carry a bit of this voice's
    // chorused character to the shared reverb/delay busses, not just its
    // plain dry signal, when active - no distance-undoing division needed
    // here (unlike a former version of this code): wetL/wetR were never
    // distance-attenuated to begin with, so sends can use getSends().a/b
    // directly, the same simplification encodePosition() itself now uses.
    auto & sends = getSends();
    if (auto * aux_a = data.getChannel(Channel::AuxA)) {
      for (int i = 0; i < totalSamples; i++) aux_a[i] += 0.5f * (wetL[i] + wetR[i]) * sends.a;
    }
    if (auto * aux_b = data.getChannel(Channel::AuxB)) {
      for (int i = 0; i < totalSamples; i++) aux_b[i] += 0.5f * (wetL[i] + wetR[i]) * sends.b;
    }

    return data;
  }
};

AudioBuffer
SoundFontVoice::render(int numSamples) {
  auto f = sf_.get();

  int totalSamples = numSamples;
  dry_.assign(static_cast<size_t>(totalSamples), 0.0f); // zeroed: we don't fill past stopping playing

  if (!isActive()) {
    return encodeWithChorus(totalSamples);
  }

  int writeIndex = 0;
  auto input = f->fontSamples_;
  
  // hasChannelPressureCutoffMod_/hasChannelPressurePitchMod_ can make an
  // LFO/env relevant even when every *static* generator that would
  // normally imply it is zero - a file can author a channel-pressure ->
  // VibLfoToPitch connection while leaving the static vibLfoToPitch
  // generator itself at 0, relying entirely on the modulator. Without
  // this, that LFO/env would never advance past its initial (silent)
  // level and the channel-pressure modulator below would multiply by a
  // permanently frozen value instead of a real oscillation/envelope.
  bool updateModEnv = (voiceRegion_->modEnvToPitch || voiceRegion_->modEnvToFilterFc || hasChannelPressureCutoffMod_ || hasChannelPressurePitchMod_);
  bool updateModLFO = (modlfo_.getDelta() && (voiceRegion_->modLfoToPitch || voiceRegion_->modLfoToFilterFc || voiceRegion_->modLfoToVolume || hasChannelPressureCutoffMod_ || hasChannelPressurePitchMod_));
  bool updateVibLFO = (viblfo_.getDelta() && (voiceRegion_->vibLfoToPitch || hasChannelPressurePitchMod_));
  bool isLooping    = (loopStart_ < loopEnd_);
  double sampleEndDbl = (double)voiceRegion_->end;
  double loopEndDbl = (double)loopEnd_ + 1.0;
  bool dynamicGain = (voiceRegion_->modLfoToVolume != 0);
  float sampleRate = getChannelConfiguration().getAudioOutSampleRate();
  bool dynamicLowpass = (voiceRegion_->modLfoToFilterFc || voiceRegion_->modEnvToFilterFc || hasChannelPressureCutoffMod_);
  bool dynamicPitchRatio = (voiceRegion_->modLfoToPitch || voiceRegion_->modEnvToPitch || voiceRegion_->vibLfoToPitch || hasChannelPressurePitchMod_);
  
  auto tmpInitialFilterFc = 0, tmpModLfoToFilterFc = 0, tmpModEnvToFilterFc = 0;
  if (dynamicLowpass) {
    tmpInitialFilterFc = (float)voiceRegion_->initialFilterFc;
    tmpModLfoToFilterFc = (float)voiceRegion_->modLfoToFilterFc;
    tmpModEnvToFilterFc = (float)voiceRegion_->modEnvToFilterFc;
  }

  auto tmpModLfoToPitch = 0.0f, tmpVibLfoToPitch = 0.0f, tmpModEnvToPitch = 0.0f;
  auto pitchRatio = 0.0;
  if (dynamicPitchRatio) {
    tmpModLfoToPitch = (float)voiceRegion_->modLfoToPitch;
    tmpVibLfoToPitch = (float)voiceRegion_->vibLfoToPitch;
    tmpModEnvToPitch = (float)voiceRegion_->modEnvToPitch;
  } else {
    pitchRatio = tsf_timecents2Secsd(pitchInputTimecents_) * pitchOutputFactor_;
  }

  auto noteGain = 0.0f, tmpModLfoToVolume = 0.0f;
  if (dynamicGain) {
    tmpModLfoToVolume = (float)voiceRegion_->modLfoToVolume * 0.1f;
  } else {
    noteGain = decibelsToGain(getGainDB());
  }

  while (numSamples) {
    int blockSamples = (numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples);
    numSamples -= blockSamples;

    // Channel-pressure modulator contributions, added to the same "depth"
    // variables the static generators already feed into below - a
    // destination that scales an LFO/env (ModLfoToFilterFc/
    // ModEnvToFilterFc/ModLfoToPitch/VibLfoToPitch/ModEnvToPitch) adds to
    // that depth (still scaled by the LFO/env's own current level in the
    // formulas below), while one that sets a base value directly
    // (InitialFilterFc/CoarseTune/FineTune) adds straight into the base.
    // Only scanned when this region actually has a relevant modulator
    // (hasChannelPressureCutoffMod_/hasChannelPressurePitchMod_, set once
    // at construction) - a region with none pays nothing extra here.
    float cpInitialFilterFc = 0.0f, cpModLfoToFilterFc = 0.0f, cpModEnvToFilterFc = 0.0f;
    float cpModLfoToPitch = 0.0f, cpVibLfoToPitch = 0.0f, cpModEnvToPitch = 0.0f, cpPitchTimecents = 0.0f;
    if (hasChannelPressureCutoffMod_ || hasChannelPressurePitchMod_) {
      for (auto & c : voiceRegion_->modulators) {
	if (!SF2Mod::isChannelPressureSourced(c)) continue;
	float contribution = SF2Mod::evaluateChannelPressureModulator(c, sf2_channel_pressure_);
	switch (c.dest) {
	case 5:  cpModLfoToPitch += contribution; break;
	case 6:  cpVibLfoToPitch += contribution; break;
	case 7:  cpModEnvToPitch += contribution; break;
	case 8:  cpInitialFilterFc += contribution; break;
	case 10: cpModLfoToFilterFc += contribution; break;
	case 11: cpModEnvToFilterFc += contribution; break;
	case 51: cpPitchTimecents += contribution * 100.0f; break; // CoarseTune: semitones -> cents
	case 52: cpPitchTimecents += contribution; break; // FineTune: already cents
	}
      }
    }

    if (dynamicLowpass) {
      float fres = (tmpInitialFilterFc + cpInitialFilterFc)
                 + modlfo_.getLevel() * (tmpModLfoToFilterFc + cpModLfoToFilterFc)
                 + modenv_.getLevel() * (tmpModEnvToFilterFc + cpModEnvToFilterFc);
      float lowpassFc = (fres <= 13500 ? tsf_cents2Hertz(fres) / sampleRate : 1.0f);
      lowpass_.active_ = (lowpassFc < 0.499f);
      if (lowpass_.active_) lowpass_.setFc(lowpassFc);
    }

    if (dynamicPitchRatio) {
      pitchRatio = tsf_timecents2Secsd(pitchInputTimecents_ + cpPitchTimecents
        + (modlfo_.getLevel() * (tmpModLfoToPitch + cpModLfoToPitch)
           + viblfo_.getLevel() * (tmpVibLfoToPitch + cpVibLfoToPitch)
           + modenv_.getLevel() * (tmpModEnvToPitch + cpModEnvToPitch))) * pitchOutputFactor_;
    }

    if (dynamicGain) {
      noteGain = decibelsToGain(getGainDB() + (modlfo_.getLevel() * tmpModLfoToVolume));
    }

    // No getDistanceGain() here - encodePosition()/encodeWithChorus() apply
    // distance attenuation themselves now (see InstrumentVoice.h's own doc
    // comment on encodePosition()).
    auto dryGainMono = noteGain * ampenv_.getLevel();

    // Silence-kill threshold: a long-tailed release (common for GM pad/
    // string patches) can sit well below audibility for most of its
    // authored duration, still consuming full render cost every block.
    // isReleasing() gates this to the release stage only - a held note
    // can legitimately be this quiet during ATTACK or a deliberately
    // quiet SUSTAIN and must never be killed early regardless. Jumping
    // straight to DONE (same transition a naturally-completed release
    // already makes) reuses the exact path isActive()/
    // clearFinishedVoices() already rely on - no separate reaping logic.
    if (ampenv_.isReleasing() && gainToDecibels(dryGainMono) < constants::SILENCE_KILL_FLOOR_DB) {
      ampenv_.nextSegment(EnvelopeState::RELEASE);
      // Keep both envelopes consistent, matching killNote()'s existing
      // precedent - isDone()-guarded for the same reason stopNote() guards
      // it: nextSegment() unconditionally forces a transition regardless
      // of current state, which would resurrect modenv_ if it had somehow
      // already reached DONE independently of ampenv_.
      if (!modenv_.isDone()) modenv_.nextSegment(EnvelopeState::RELEASE);
    }

    // Update EG.
    ampenv_.process(blockSamples);
    if (updateModEnv) modenv_.process(blockSamples);

    // Update LFOs.
    if (updateModLFO) modlfo_.process(blockSamples);
    if (updateVibLFO) viblfo_.process(blockSamples);

    // Interpolate the block's gain sample-by-sample (dryGainMono at the
    // block's start ramping to noteGain * ampenv_'s now-updated level)
    // rather than holding dryGainMono flat across the whole block - see
    // EnvelopeFilterState::applyEffect()'s identical fix and its own doc
    // comment for why: RENDER_EFFECTSAMPLEBLOCK (64 samples) is coarse
    // relative to a fast-released envelope's ~10ms/441-sample exponential
    // decay (~26% level drop per block), so a flat per-block gain is an
    // audible staircase there, not just an inaudible quantization step.
    // Only the ampenv_-driven part is swept - noteGain itself (constant
    // here unless dynamicGain, which already only updates once per block
    // like before) is unaffected.
    float dryGainEnd = noteGain * ampenv_.getLevel();
    float dryGainStep = blockSamples > 0 ? (dryGainEnd - dryGainMono) / static_cast<float>(blockSamples) : 0.0f;

    while (blockSamples-- && sourceSamplePosition_ < sampleEndDbl) {
      unsigned int pos = (unsigned int)sourceSamplePosition_, nextPos = (pos >= loopEnd_ && isLooping ? loopStart_ : pos + 1);

      // Simple linear interpolation.
      float alpha = (float)(sourceSamplePosition_ - pos);
      float val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);

      // Low-pass filter.
      if (lowpass_.active_) val = lowpass_.process(val);

      dry_[static_cast<size_t>(writeIndex++)] = val * dryGainMono;
      dryGainMono += dryGainStep;

      // Next sample.
      sourceSamplePosition_ += pitchRatio;
      if (sourceSamplePosition_ >= loopEndDbl && isLooping) sourceSamplePosition_ -= (loopEnd_ - loopStart_ + 1.0);
    }
    
    if (!isActive()) {
      break;
    }
  }

  return encodeWithChorus(totalSamples);
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
    res->presets_.resize(presetNum);
    res->fontSamples_ = fontSamples;
    fontSamples = nullptr; // don't free below
    tsf_load_presets(res, &hydra, fontSampleCount);
  }
  free(hydra.phdrs); free(hydra.pbags); free(hydra.pmods);
  free(hydra.pgens); free(hydra.insts); free(hydra.ibags);
  free(hydra.imods); free(hydra.igens); free(hydra.shdrs);
  free(fontSamples);
}

void
SoundFont::openFile() {
  sf_ = make_shared<SoundFontFile>(filename_);
}

namespace {

struct PercussionOffset { float u, v; };

// GM percussion key -> (u, v) normalized position offset (fractions of
// the kit's own extent, horizontal/vertical), indexed by (midiKey - 27) -
// same indexing convention as Note.h's percussion_names[]. One shared
// table, every GM percussion key (27-82), not just the "core rock kit"
// ones - mixing families (a rock kit and a Latin percussion set) on the
// same track is the artist's own responsibility to avoid, not something
// this table guards against. Compiled-in, no XML surface at all - a
// first-pass tuning target, not acoustically final.
constexpr PercussionOffset kPercussionOffsets[56] = {
  { 0.1f, 0.05f },    // 27 High Q
  { -0.1f, 0.05f },   // 28 Slap
  { 0.2f, -0.05f },   // 29 Scratch Push
  { -0.2f, -0.05f },  // 30 Scratch Pull
  { 0.05f, -0.1f },   // 31 Sticks
  { -0.05f, -0.1f },  // 32 Square Click
  { 0.0f, 0.15f },    // 33 Metronome Click
  { 0.0f, 0.25f },    // 34 Metronome Bell
  { 0.0f, -0.4f },    // 35 Acoustic Bass Drum
  { 0.0f, -0.4f },    // 36 Bass Drum 1
  { 0.15f, -0.1f },   // 37 Side Stick
  { 0.15f, -0.1f },   // 38 Acoustic Snare
  { -0.15f, -0.1f },  // 39 Hand Clap
  { 0.15f, -0.1f },   // 40 Electric Snare
  { -0.5f, 0.0f },    // 41 Low Floor Tom
  { 0.55f, 0.3f },    // 42 Closed Hi-Hat
  { -0.5f, 0.0f },    // 43 High Floor Tom
  { 0.55f, 0.1f },    // 44 Pedal Hi-Hat
  { -0.2f, 0.05f },   // 45 Low Tom
  { 0.55f, 0.35f },   // 46 Open Hi-Hat
  { -0.2f, 0.05f },   // 47 Low-Mid Tom
  { 0.3f, 0.1f },     // 48 Hi-Mid Tom
  { -0.7f, 0.5f },    // 49 Crash Cymbal 1
  { 0.3f, 0.1f },     // 50 High Tom
  { 0.65f, 0.4f },    // 51 Ride Cymbal 1
  { 0.85f, 0.5f },    // 52 Chinese Cymbal
  { 0.65f, 0.35f },   // 53 Ride Bell
  { 0.1f, 0.2f },     // 54 Tambourine
  { -0.85f, 0.5f },   // 55 Splash Cymbal
  { -0.6f, 0.1f },    // 56 Cowbell
  { 0.75f, 0.5f },    // 57 Crash Cymbal 2
  { -0.7f, -0.1f },   // 58 Vibraslap
  { 0.65f, 0.4f },    // 59 Ride Cymbal 2
  { 0.4f, 0.15f },    // 60 Hi Bongo
  { 0.2f, -0.05f },   // 61 Low Bongo
  { -0.3f, 0.0f },    // 62 Mute High Conga
  { -0.15f, 0.05f },  // 63 Open High Conga
  { -0.45f, -0.15f }, // 64 Low Conga
  { 0.55f, 0.2f },    // 65 High Timbale
  { 0.4f, 0.0f },     // 66 Low Timbale
  { 0.7f, 0.35f },    // 67 High Agogo
  { 0.55f, 0.15f },   // 68 Low Agogo
  { 0.0f, 0.1f },     // 69 Cabasa
  { 0.15f, 0.1f },    // 70 Maracas
  { -0.6f, 0.3f },    // 71 Short Whistle
  { -0.7f, 0.35f },   // 72 Long Whistle
  { -0.4f, -0.1f },   // 73 Short Guiro
  { -0.5f, -0.1f },   // 74 Long Guiro
  { 0.0f, 0.0f },     // 75 Claves
  { 0.3f, 0.05f },    // 76 Hi Wood Block
  { 0.15f, -0.05f },  // 77 Low Wood Block
  { -0.2f, -0.2f },   // 78 Mute Cuica
  { -0.3f, -0.2f },   // 79 Open Cuica
  { 0.6f, 0.4f },     // 80 Mute Triangle
  { 0.65f, 0.42f },   // 81 Open Triangle
  { 0.05f, 0.15f },   // 82 Shaker
};

// Bank-0 GM program numbers whose keys are physically separated
// radiators - a real per-key position to sweep across, not just a
// bigger point source - shared by SoundFontInstrument::getDefaultExtent()
// (their nonzero default extent) and SoundFontInstrument::playNote()
// (which, like the percussion-offset mechanism, resolves these
// instruments' position via the pitched arc rather than the region's own
// native SF2 pan - see SoundFontVoice's skip_native_pan): piano family
// (0-7), glockenspiel/vibraphone/marimba/xylophone/tubular bells (9,
// 11-14 - mallet instruments, each bar its own radiator), orchestral harp
// (46, each string its own radiator), and timpani (47, a set - each drum
// its own radiator). Other chromatic percussion (celesta 8, music box 10,
// dulcimer 15) and every organ (16-20) also get a nonzero default extent
// in getDefaultExtent() but are deliberately not part of this set - they
// have no per-key arc, just a plain (larger) point-ish source, so their
// regions keep using the native pan as before.
bool isPitchedArcFamily(const tsf_preset & preset) {
  if (preset.bank != 0) return false;
  return preset.preset <= 7 || preset.preset == 9 || (preset.preset >= 11 && preset.preset <= 14) || preset.preset == 46 || preset.preset == 47;
}

// Fixed compile-time seed, not drawn from the shared getRandF()/rand()
// sequence - see NoteMultiplier's own phase randomization and
// SongState's velocity/delay randomization, both of which already
// consume that shared sequence at unpredictable, call-order-dependent
// points, so seeding jitter from it would make jitter values depend on
// unrelated musical randomization elsewhere in the render. Matches
// bus/GranularCloud.cpp's own kDirectionScatterSeed precedent: a fixed
// constant chosen so a full re-render reproduces bit-identical jitter.
constexpr uint32_t kPercussionJitterSeed = 0x2545f491u;

// Converts a normalized (u, v) offset (fractions of the instrument's own
// extent, horizontal/vertical) into a real azimuth/elevation delta and
// adds it to `position` - the one shared algebra behind both the
// percussion table and the pitched arc below (also used, independently,
// by the floor reflection and NoteMultiplier's own scatter):
// x = u*extent, y = v*extent/kExtentShapeRatio, delta = atan2(x or y,
// distance). A zero-extent instrument (a point source - nothing to
// offset within) or no position ever set at all (distance <= 0, same
// "nothing to attach a direction to" reasoning computeAmbisonicGains()
// itself uses) leave `position` untouched. Perspective mirror is
// computed fresh from distance, not stored - <= 1 reads as "player"
// (u/v used as given), > 1 as "audience" (mirrored) - no live per-note
// distance edit path exists in this codebase, so this can never flip
// mid-note.
SphericalPosition applyNormalizedOffset(const SphericalPosition & position, float u, float v) {
  if (position.extent <= 0.0f || position.distance <= 0.0f) return position;

  float mirror_sign = position.distance <= 1.0f ? 1.0f : -1.0f;
  float x = u * position.extent;
  float y = v * position.extent / kExtentShapeRatio;
  constexpr float kRad2Deg = 180.0f / static_cast<float>(M_PI);

  SphericalPosition result = position;
  result.azimuth += atan2f(mirror_sign * x, position.distance) * kRad2Deg;
  result.elevation += atan2f(y, position.distance) * kRad2Deg;
  return result;
}

// Applies a GM percussion key's own position offset - table lookup plus
// a small per-hit jitter, then applyNormalizedOffset() above. midiKey
// outside the GM percussion range leaves `position` untouched (the rest
// of the "does nothing" contract - zero extent, no position set - is
// applyNormalizedOffset()'s own). `jitter_counter` is the calling
// SoundFontInstrument's own mutable per-instance counter (advanced once
// per call here) - not a per-note identity, just enough to keep repeated
// hits of the same key from landing on an identical point, while a full
// re-render from scratch still reproduces bit-identical results (the
// counter always starts back at 0).
SphericalPosition applyPercussionOffset(const SphericalPosition & position, int midiKey, uint32_t & jitter_counter) {
  if (midiKey < 27 || midiKey > 82) return position;

  auto & offset = kPercussionOffsets[static_cast<size_t>(midiKey - 27)];

  NoiseGenerator rng(kPercussionJitterSeed ^ (static_cast<uint32_t>(midiKey) * 2654435761u) ^ (jitter_counter * 0x9e3779b9u));
  jitter_counter++;
  constexpr float kJitterScale = 0.05f; // per-feature multiplier on extent - a small nudge, not a repositioning
  float u = offset.u + rng.next() * kJitterScale;
  float v = offset.v + rng.next() * kJitterScale;

  return applyNormalizedOffset(position, u, v);
}

// Arc tilt: how much the arc's vertical component follows its horizontal
// one (v = u * kArcTilt) - 0 means a flat arc (no elevation sweep at
// all), matching the "elevation is garnish" reasoning the percussion
// table's own vertical offsets already follow. A fixed constant, not a
// per-song parameter - this whole mechanism is zero-config/hardcoded, no
// XML surface, same as the percussion table above.
constexpr float kArcTilt = 0.0f;

// Applies a pitched instrument's own position along its key-range arc:
// `midiKey`'s position within the instrument's actual mapped key range
// (lokeyMin/hikeyMax, the union of every region's own lokey/hikey - see
// the call site) becomes u = 2*(key-lokeyMin)/(hikeyMax-lokeyMin) - 1,
// i.e. -1 at the lowest mapped key, +1 at the highest, then
// applyNormalizedOffset() above (same mirror/extent algebra as the
// percussion table - the mirror flips which end holds the low notes, not
// a separate parameter). No jitter - a piano/harp's key positions are
// fixed points along the instrument, not independently-placed hits.
// lokeyMin >= hikeyMax (a degenerate/empty key range) leaves `position`
// untouched, same as a zero-extent instrument.
SphericalPosition applyPitchedArcOffset(const SphericalPosition & position, int midiKey, int lokeyMin, int hikeyMax) {
  if (lokeyMin >= hikeyMax) return position;

  float u = 2.0f * static_cast<float>(midiKey - lokeyMin) / static_cast<float>(hikeyMax - lokeyMin) - 1.0f;
  if (u < -1.0f) u = -1.0f;
  else if (u > 1.0f) u = 1.0f;
  float v = u * kArcTilt;

  return applyNormalizedOffset(position, u, v);
}

}

class SoundFontInstrument : public Instrument {
public:
  SoundFontInstrument(std::shared_ptr<SoundFontFile> sf, size_t preset) : sf_(sf), preset_(preset) { }

  const char * getElementName() const override { return "soundFontInstrument"; }

  // Family default extents (meters, half-width) by GM bank/program - bank
  // 128 is the GM percussion-bank convention (a drum kit); every other
  // entry is a GM program number (bank 0) whose instrument has a real,
  // physically laid-out width (a keyboard, a row of bars/pipes/bells) -
  // everything not listed defaults to 0, a point source, same as any
  // instrument without a documented physical size, unless the artist
  // sets an explicit extent on the track.
  float getDefaultExtent() const override {
    if (preset_ >= sf_->presets_.size()) return 0.0f;
    auto & preset = sf_->presets_[preset_];
    if (preset.bank == 128) return 1.2f; // GM percussion kit
    if (preset.bank != 0) return 0.0f;
    switch (preset.preset) {
      // Piano family (0-7): Acoustic/Bright/Electric Grand, Honky-tonk,
      // Electric Piano 1/2, Harpsichord, Clavinet.
      case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: return 1.5f;
      // Chromatic percussion (8-15): keyboard/bar-laid-out mallet
      // instruments, sized per instrument rather than one flat value -
      // a music box is tiny, a marimba or tubular bells row is nearly
      // piano-sized. 9/11/12/13 are also pitched-arc family (isPitchedArcFamily())
      // - their per-key position isn't just a bigger point source, it
      // sweeps across this same span.
      case 8: return 0.6f;  // Celesta
      case 9: return 0.65f; // Glockenspiel - arc family (shares Xylophone's span)
      case 10: return 0.2f; // Music Box
      case 11: return 1.2f; // Vibraphone - arc family (shares Marimba's span)
      case 12: return 1.2f; // Marimba - arc family
      case 13: return 0.65f; // Xylophone - arc family (shares Glockenspiel's span)
      case 14: return 0.6f; // Tubular Bells - arc family
      case 15: return 0.8f; // Dulcimer
      // Organs (16-20): console instruments are roughly piano-sized;
      // a real Church Organ's pipes span far wider than any console.
      case 16: case 17: case 18: return 1.5f; // Drawbar/Percussive/Rock Organ
      case 19: return 3.0f; // Church Organ
      case 20: return 1.2f; // Reed Organ
      // Harp (46): Orchestral Harp - arc family.
      case 46: return 0.5f;
      // Timpani (47, a set): arc family - the pitches sweep across the
      // set's own physical layout, same reasoning as the mallet family.
      case 47: return 0.8f;
      default: return 0.0f;
    }
  }

  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, const SendLevels & sends) const override {
    assert(frequency > 0);

    detune *= getHarmonic();
    detune /= getSubharmonic();

    vector<pair<int, unique_ptr<TrackState> > > voices;

    auto f = sf_.get();
    if (preset_ <= f->presets_.size()) {
      // don't use detune for sample selection
      auto midiKey = int(round(log2(frequency / 440) * 12 + 69));
      auto midiVelocity = (short)(velocity * 127);
      if (midiVelocity > 127) midiVelocity = 127;

      // GM percussion kit (bank 128) - apply this key's own compiled-in
      // position offset once, before the per-region loop below, so every
      // region matching this note-on (velocity layers etc.) shares the
      // same resolved position. Anything else (a pitched preset, or no
      // preset at all) leaves `position` untouched entirely - see
      // applyPercussionOffset()'s own doc comment for the full contract.
      bool is_percussion = preset_ < f->presets_.size() && f->presets_[preset_].bank == 128;
      bool is_arc = preset_ < f->presets_.size() && isPitchedArcFamily(f->presets_[preset_]);

      SphericalPosition adjusted_position = position;
      if (is_percussion) {
	adjusted_position = applyPercussionOffset(position, midiKey, jitter_counter_);
      } else if (is_arc) {
	// The instrument's actual mapped key range - the union of every
	// region's own lokey/hikey, not a single region's (a real preset
	// commonly layers several regions, e.g. per velocity, each
	// covering the same overall range) - so the arc spans the whole
	// playable instrument, not just whichever region happens to be
	// scanned first.
	int lokeyMin = 127, hikeyMax = 0;
	for (auto & region : f->presets_[preset_].regions) {
	  lokeyMin = std::min<int>(lokeyMin, region.lokey);
	  hikeyMax = std::max<int>(hikeyMax, region.hikey);
	}
	adjusted_position = applyPitchedArcOffset(position, midiKey, lokeyMin, hikeyMax);
      }

      // Percussion/pitched-arc instruments also skip the region's own
      // native SF2 pan below (TEMPORARY - see SoundFontVoice's ctor doc
      // comment) so they play from exactly their resolved position;
      // everything else (other pitched presets, unmatched banks) is
      // untouched and keeps the native pan exactly as before.
      bool position_resolved_by_new_mechanism = is_percussion || is_arc;

      // Play all matching regions.

      auto & regions = f->presets_[preset_].regions;

      for (size_t region_idx = 0; region_idx < regions.size(); region_idx++) {
	auto & region = regions[region_idx];
	if (midiKey < region.lokey || midiKey > region.hikey || midiVelocity < region.lovel || midiVelocity > region.hivel) continue;

	// Exclusive-class choking (region.group, gen 57) can't happen here -
	// this factory method has no visibility into any other track voice,
	// so it can't reach across separate note-on events (open hi-hat,
	// then later a closed hi-hat). See InstrumentTrackState::
	// chokeExclusiveClasses(), which enforces it at the one layer that
	// actually can: TrackState::getExclusiveClasses() exposes this
	// region's own class to that caller.

	// Each region's own position (folded in via SoundFontVoice::getPosition())
	// gets encoded directly by the voice itself (InstrumentVoice::encodePosition()).
	// adjusted_position (not the raw incoming position) already carries
	// this key's own percussion offset, if any. position_resolved_by_new_mechanism
	// also skips SoundFontVoice's own native-pan folding (TEMPORARY - see
	// its ctor's doc comment) so percussion/pitched-arc instruments play
	// from exactly their resolved position; every other instrument keeps
	// that folding.
	auto voice = make_unique<SoundFontVoice>(channel_config, adjusted_position, detune, start_phase, sf_, preset_, region_idx, sends, position_resolved_by_new_mechanism);
	voice->playNote(frequency, velocity, note_value);

	if (!getChildren().empty()) {
	  // create modulators for voice - see SendLevels.h's own doc comment
	  // for why SendLevels{} (not sends) is correct here.
	  for (auto & child : getChildren()) {
	    auto modulator = child->playNote(channel_config, SphericalPosition{}, frequency, detune, velocity, start_phase, note_value, SendLevels{});
	    if (modulator.get()) voice->addChild(child->getInternalId(), move(modulator));
	  }
	}

	// Keyed by region_idx, not getInternalId() (constant across every
	// region this loop matches) - addChild()'s children_[id] = ... would
	// otherwise overwrite (and destroy) each earlier region's voice as
	// soon as a second region matched, silently dropping all but the
	// last one instead of layering them.
	voices.emplace_back(static_cast<int>(region_idx), move(voice));
      }
    }

    if (voices.size() == 1) {
      return move(voices[0].second);
    } else {
      // Unreduced channel_config: this group's own true output format is
      // whatever it was asked for (matching what its caller expects back).
      // Each region-voice inside is already MONO/STEREO (above); the
      // plain TrackState's generic render(int frames) FOA-encodes each one
      // individually using its own (region-pan-adjusted) position as soon
      // as it notices the channel-count mismatch - no group-state override
      // needed here, same reasoning as NoteMultiplier.
      auto group = make_unique<TrackState>(channel_config);
      for (auto & [ id, voice ] : voices) group->addChild(id, move(voice));
      return group;
    }
  }
  
private:
  shared_ptr<SoundFontFile> sf_;
  size_t preset_;

  // applyPercussionOffset()'s own per-hit jitter counter - advanced once
  // per note-on (mutable since playNote() is const), never a per-note
  // identity, just enough that repeated hits of the same key don't land
  // on an identical point. Starts at 0 for every fresh instance, so a
  // full re-render from scratch reproduces bit-identical jitter.
  mutable uint32_t jitter_counter_ = 0;
};

std::unique_ptr<Instrument>
SoundFont::createInstrument(size_t preset, const char * name) {
  auto instrument = make_unique<SoundFontInstrument>(sf_, preset);
  instrument->setName(name ? name : sf_->getPresetName(preset));
  return instrument;
}

std::vector<std::unique_ptr<Instrument> >
SoundFont::createAll() {
  std::vector<std::unique_ptr<Instrument> > r;
  size_t n = sf_->getPresetCount();
  for (size_t i = 0; i < n; i++) r.push_back(createInstrument(i));
  return r;
}
