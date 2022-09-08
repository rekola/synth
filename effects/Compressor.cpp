// (c) Copyright 2016, Sean Connelly (@velipso), https://sean.cm
// MIT License
// Project Home: https://github.com/velipso/sndfilter

// dynamics compressor based on WebAudio specification:
//   https://webaudio.github.io/web-audio-api/#the-dynamicscompressornode-interface

// dynamic range compression is a complex topic with many different algorithms
//
// this API works by first initializing an sf_compressor_state_st structure, then using it to
// process a sample in chunks
//
// for example, say you're processing a stream in 128 samples per chunk:
//
//   sf_compressor_state_st simplecomp;
//   sf_simplecomp(&simplecomp, 48000, 5, -24, 30, 12, 0.003f, 0.250f);
//
//   for each 128 length sample:
//     sf_compressor_process(&simplecomp, 128, input, output);
//
// notice that sf_compressor_process will change a lot of the member variables inside of the state
// structure, since these values must be carried over across chunk boundaries
//
// also notice that the choice to divide the sound into chunks of 128 samples is completely
// arbitrary from the compressor's perspective, however, the size should be divisible by the SPU
// value below (defaults to 32):

// core algorithm extracted from Chromium source, DynamicsCompressorKernel.cpp, here:
//   https://git.io/v1uSK
//
// changed a few things though in an attempt to simplify the curves and algorithm, and also included
// a pregain so that samples can be scaled up then compressed


#include "Compressor.h"

#include "EffectState.h"

#include <cmath>
#include <cstddef>

// maximum number of samples in the delay buffer
#define SF_COMPRESSOR_MAXDELAY   1024

// samples per update; the compressor works by dividing the input chunks into even smaller sizes,
// and performs heavier calculations after each mini-chunk to adjust the final envelope
#define SF_COMPRESSOR_SPU        32

// not sure what this does exactly, but it is part of the release curve
#define SF_COMPRESSOR_SPACINGDB  5.0f

static inline float db2lin(float db) { // dB to linear
  return powf(10.0f, 0.05f * db);
}

static inline float lin2db(float lin) { // linear to dB
  return 20.0f * log10f(lin);
}

// for more information on the knee curve, check out the compressor-curve.html demo + source code
// included in this repo
static inline float kneecurve(float x, float k, float linearthreshold) {
  return linearthreshold + (1.0f - expf(-k * (x - linearthreshold))) / k;
}

static inline float kneeslope(float x, float k, float linearthreshold) {
  return k * x / ((k * linearthreshold + 1.0f) * expf(k * (x - linearthreshold)) - 1);
}

static inline float compcurve(float x, float k, float slope, float linearthreshold,
			      float linearthresholdknee, float threshold, float knee, float kneedboffset) {
  if (x < linearthreshold)
    return x;
  if (knee <= 0.0f) // no knee in curve
    return db2lin(threshold + slope * (lin2db(x) - threshold));
  if (x < linearthresholdknee)
    return kneecurve(x, k, linearthreshold);
  return db2lin(kneedboffset + slope * (lin2db(x) - threshold - knee));
}

// for more information on the adaptive release curve, check out adaptive-release-curve.html demo +
// source code included in this repo
static inline float adaptivereleasecurve(float x, float a, float b, float c, float d) {
  // a*x^3 + b*x^2 + c*x + d
  float x2 = x * x;
  return a * x2 * x + b * x2 + c * x + d;
}

static inline float clampf(float v, float min, float max) {
  return v < min ? min : (v > max ? max : v);
}

static inline float absf(float v) {
  return v < 0.0f ? -v : v;
}

static inline float fixf(float v, float def) {
  // fix NaN and infinity values that sneak in... not sure why this is needed, but it is
  if (std::isnan(v) || std::isinf(v)) return def;
  return v;
}

using namespace std;

class CompressorState : public EffectState {
public:
  // populate a compressor state with all default values
  CompressorState(const ChannelConfiguration & _channel_config)
    : EffectState(_channel_config)
  {
    initialize( 0.000f, // pregain
		-24.000f, // threshold
		30.000f, // knee
		12.000f, // ratio
		0.003f, // attack
		0.250f, // release
		0.006f, // predelay
		0.090f, // releasezone1
		0.160f, // releasezone2
		0.420f, // releasezone3
		0.980f, // releasezone4
		0.000f, // postgain
		1.000f  // wet
		);
  }

  // populate a compressor state with simple parameters
  CompressorState(const ChannelConfiguration & _channel_config,
		  float pregain,   // dB, amount to boost the signal before applying compression [0 to 100]
		  float threshold, // dB, level where compression kicks in [-100 to 0]
		  float knee,      // dB, width of the knee [0 to 40]
		  float ratio,     // unitless, amount to inversely scale the output when applying comp [1 to 20]
		  float attack,    // seconds, length of the attack phase [0 to 1]
		  float release    // seconds, length of the release phase [0 to 1]
		  )
    : EffectState(_channel_config)
  {
    initialize(pregain, threshold, knee, ratio, attack, release,
	       0.006f, // predelay
	       0.090f, // releasezone1
	       0.160f, // releasezone2
	       0.420f, // releasezone3
	       0.980f, // releasezone4
	       0.000f, // postgain
	       1.000f  // wet
	       );
  }
  
  // populate a compressor state with advanced parameters
  CompressorState(const ChannelConfiguration & channel_config,
		  // these parameters are the same as the simple version above:
		  float pregain, float threshold, float knee, float ratio, float attack, float release,
		  // these are the advanced parameters:
		  float predelay,     // seconds, length of the predelay buffer [0 to 1]
		  float releasezone1, // release zones should be increasing between 0 and 1, and are a fraction
		  float releasezone2, //  of the release time depending on the input dB -- these parameters define
		  float releasezone3, //  the adaptive release curve, which is discussed in further detail in the
		  float releasezone4, //  demo: adaptive-release-curve.html
		  float postgain,     // dB, amount of gain to apply after compression [0 to 100]
		  float wet           // amount to apply the effect [0 completely dry to 1 completely wet]
		  )
    : EffectState(channel_config)
  {
    initialize(pregain, threshold, knee, ratio, attack, release,
	       predelay,
	       releasezone1, releasezone2, releasezone3, releasezone4,
	       postgain,
	       wet
	       );
  }

  
  void applyEffect(SampleData & input) override {
    // pull out the state into local variables
    float metergain            = metergain_;
    float meterrelease         = meterrelease_;
    float threshold            = threshold_;
    float knee                 = knee_;
    float linearpregain        = linearpregain_;
    float linearthreshold      = linearthreshold_;
    float slope                = slope_;
    float attacksamplesinv     = attacksamplesinv_;
    float satreleasesamplesinv = satreleasesamplesinv_;
    float wet                  = wet_;
    float dry                  = dry_;
    float k                    = k_;
    float kneedboffset         = kneedboffset_;
    float linearthresholdknee  = linearthresholdknee_;
    float mastergain           = mastergain_;
    float a                    = a_;
    float b                    = b_;
    float c                    = c_;
    float d                    = d_;
    float detectoravg          = detectoravg_;
    float compgain             = compgain_;
    float maxcompdiffdb        = maxcompdiffdb_;
    int delaybufsize           = delaybufsize_;
    int delaywritepos          = delaywritepos_;
    int delayreadpos           = delayreadpos_;
    auto left_delaybuf = delaybuf_.getChannelData(0);
    auto right_delaybuf = delaybuf_.getChannelData(1);

    auto left_input = input.getChannelData(0);
    auto right_input = input.getChannelData(1);

    int samplesperchunk = SF_COMPRESSOR_SPU;
    int chunks = input.numberOfFrames() / samplesperchunk;
    float ang90 = (float)M_PI * 0.5f;
    float ang90inv = 2.0f / (float)M_PI;
    int samplepos = 0;
    float spacingdb = SF_COMPRESSOR_SPACINGDB;

    for (int ch = 0; ch < chunks; ch++) {
      detectoravg = fixf(detectoravg, 1.0f);
      float desiredgain = detectoravg;
      float scaleddesiredgain = asinf(desiredgain) * ang90inv;
      float compdiffdb = lin2db(compgain / scaleddesiredgain);

      // calculate envelope rate based on whether we're attacking or releasing
      float enveloperate;
      if (compdiffdb < 0.0f) { // compgain < scaleddesiredgain, so we're releasing
	compdiffdb = fixf(compdiffdb, -1.0f);
	maxcompdiffdb = -1; // reset for a future attack mode
	// apply the adaptive release curve
	// scale compdiffdb between 0-3
	float x = (clampf(compdiffdb, -12.0f, 0.0f) + 12.0f) * 0.25f;
	float releasesamples = adaptivereleasecurve(x, a, b, c, d);
	enveloperate = db2lin(spacingdb / releasesamples);
      } else { // compresorgain > scaleddesiredgain, so we're attacking
	compdiffdb = fixf(compdiffdb, 1.0f);
	if (maxcompdiffdb == -1 || maxcompdiffdb < compdiffdb) {
	  maxcompdiffdb = compdiffdb;
	}
	float attenuate = maxcompdiffdb;
	if (attenuate < 0.5f) {
	  attenuate = 0.5f;
	}
	enveloperate = 1.0f - powf(0.25f / attenuate, attacksamplesinv);
      }

      if ((ch + 1) * samplesperchunk > input.numberOfFrames()) {
	samplesperchunk = input.numberOfFrames() * ch;
      }

      // process the chunk
      for (int chi = 0; chi < samplesperchunk; chi++, samplepos++,
	     delayreadpos = (delayreadpos + 1) % delaybufsize,
	     delaywritepos = (delaywritepos + 1) % delaybufsize) {

	float inputL = left_input[samplepos] * linearpregain;
	float inputR = right_input[samplepos] * linearpregain;
	left_delaybuf[delaywritepos] = inputL;
	right_delaybuf[delaywritepos] = inputR;

	inputL = absf(inputL);
	inputR = absf(inputR);
	float inputmax = inputL > inputR ? inputL : inputR;

	float attenuation;
	if (inputmax < 0.0001f) {
	  attenuation = 1.0f;
	} else{
	  float inputcomp = compcurve(inputmax, k, slope, linearthreshold,
				      linearthresholdknee, threshold, knee, kneedboffset);
	  attenuation = inputcomp / inputmax;
	}

	float rate;
	if (attenuation > detectoravg) { // if releasing
	  float attenuationdb = -lin2db(attenuation);
	  if (attenuationdb < 2.0f) {
	    attenuationdb = 2.0f;
	  }
	  float dbpersample = attenuationdb * satreleasesamplesinv;
	  rate = db2lin(dbpersample) - 1.0f;
	} else {
	  rate = 1.0f;
	}
	
	detectoravg += (attenuation - detectoravg) * rate;
	if (detectoravg > 1.0f) {
	  detectoravg = 1.0f;
	}
	detectoravg = fixf(detectoravg, 1.0f);

	if (enveloperate < 1) { // attack, reduce gain
	  compgain += (scaleddesiredgain - compgain) * enveloperate;
	} else { // release, increase gain
	  compgain *= enveloperate;
	  if (compgain > 1.0f) {
	    compgain = 1.0f;
	  }
	}

	// the final gain value!
	float premixgain = sinf(ang90 * compgain);
	float gain = dry + wet * mastergain * premixgain;

	// calculate metering (not used in core algo, but used to output a meter if desired)
	float premixgaindb = lin2db(premixgain);
	if (premixgaindb < metergain) {
	  metergain = premixgaindb; // spike immediately
	} else {
	  metergain += (premixgaindb - metergain) * meterrelease; // fall slowly
	}

	// apply the gain
	left_input[samplepos] = left_delaybuf[delayreadpos] * gain;
	right_input[samplepos] = right_delaybuf[delayreadpos] * gain;
      }
    }

    metergain_     = metergain;
    detectoravg_   = detectoravg;
    compgain_      = compgain;
    maxcompdiffdb_ = maxcompdiffdb;
    delaywritepos_ = delaywritepos;
    delayreadpos_  = delayreadpos;
  }
  
private:
  // this is the main initialization function
  // it does a bunch of pre-calculation so that the inner loop of signal processing is fast
  void initialize(float pregain, float threshold,
		  float knee, float ratio, float attack, float release, float predelay, float releasezone1,
		  float releasezone2, float releasezone3, float releasezone4, float postgain, float wet) {
    auto rate = getChannelConfiguration().getAudioOutSampleRate();
    
    // setup the predelay buffer
    int delaybufsize = rate * predelay;
    if (delaybufsize < 1) {
      delaybufsize = 1;
    } else if (delaybufsize > SF_COMPRESSOR_MAXDELAY) {
      delaybufsize = SF_COMPRESSOR_MAXDELAY;
    }
    delaybuf_ = SampleData(getChannelConfiguration(), delaybufsize);
    delaybuf_.zero();
    
    // useful values
    float linearpregain = db2lin(pregain);
    float linearthreshold = db2lin(threshold);
    float slope = 1.0f / ratio;
    float attacksamples = rate * attack;
    float attacksamplesinv = 1.0f / attacksamples;
    float releasesamples = rate * release;
    float satrelease = 0.0025f; // seconds
    float satreleasesamplesinv = 1.0f / ((float)rate * satrelease);
    float dry = 1.0f - wet;
    
    // metering values (not used in core algorithm, but used to output a meter if desired)
    float metergain = 1.0f; // gets overwritten immediately because gain will always be negative
    float meterfalloff = 0.325f; // seconds
    float meterrelease = 1.0f - expf(-1.0f / ((float)rate * meterfalloff));
    
    // calculate knee curve parameters
    float k = 5.0f; // initial guess
    float kneedboffset = 0.0f;
    float linearthresholdknee = 0.0f;
    if (knee > 0.0f) { // if a knee exists, search for a good k value
      float xknee = db2lin(threshold + knee);
      float mink = 0.1f;
      float maxk = 10000.0f;
      // search by comparing the knee slope at the current k guess, to the ideal slope
      for (int i = 0; i < 15; i++) {
	if (kneeslope(xknee, k, linearthreshold) < slope) {
	  maxk = k;
	} else {
	  mink = k;
	}
	k = sqrtf(mink * maxk);
      }
      kneedboffset = lin2db(kneecurve(xknee, k, linearthreshold));
      linearthresholdknee = db2lin(threshold + knee);
    }
    
    // calculate a master gain based on what sounds good
    float fulllevel = compcurve(1.0f, k, slope, linearthreshold, linearthresholdknee,
				threshold, knee, kneedboffset);
    float mastergain = db2lin(postgain) * powf(1.0f / fulllevel, 0.6f);
    
    // calculate the adaptive release curve parameters
    // solve a,b,c,d in `y = a*x^3 + b*x^2 + c*x + d`
    // interescting points (0, y1), (1, y2), (2, y3), (3, y4)
    float y1 = releasesamples * releasezone1;
    float y2 = releasesamples * releasezone2;
    float y3 = releasesamples * releasezone3;
    float y4 = releasesamples * releasezone4;
    float a = (-y1 + 3.0f * y2 - 3.0f * y3 + y4) / 6.0f;
    float b = y1 - 2.5f * y2 + 2.0f * y3 - 0.5f * y4;
    float c = (-11.0f * y1 + 18.0f * y2 - 9.0f * y3 + 2.0f * y4) / 6.0f;
    float d = y1;
    
    // save everything
    metergain_            = 1.0f; // large value overwritten immediately since it's always < 0
    meterrelease_         = meterrelease;
    threshold_            = threshold;
    knee_                 = knee;
    wet_                  = wet;
    linearpregain_        = linearpregain;
    linearthreshold_      = linearthreshold;
    slope_                = slope;
    attacksamplesinv_     = attacksamplesinv;
    satreleasesamplesinv_ = satreleasesamplesinv;
    dry_                  = dry;
    k_                    = k;
    kneedboffset_         = kneedboffset;
    linearthresholdknee_  = linearthresholdknee;
    mastergain_           = mastergain;
    a_                    = a;
    b_                    = b;
    c_                    = c;
    d_                    = d;
    detectoravg_          = 0.0f;
    compgain_             = 1.0f;
    maxcompdiffdb_        = -1.0f;
    delaybufsize_         = delaybufsize;
    delaywritepos_        = 0;
    delayreadpos_         = delaybufsize > 1 ? 1 : 0;
  }

  // user can read the metergain state variable after processing a chunk to see how much dB the
  // compressor would have liked to compress the sample; the meter values aren't used to shape the
  // sound in any way, only used for output if desired
  float metergain_;
  
  // everything else shouldn't really be mucked with unless you read the algorithm and feel
  // comfortable
  float meterrelease_;
  float threshold_;
  float knee_;
  float linearpregain_;
  float linearthreshold_;
  float slope_;
  float attacksamplesinv_;
  float satreleasesamplesinv_;
  float wet_;
  float dry_;
  float k_;
  float kneedboffset_;
  float linearthresholdknee_;
  float mastergain_;
  float a_; // adaptive release polynomial coefficients
  float b_;
  float c_;
  float d_;
  float detectoravg_;
  float compgain_;
  float maxcompdiffdb_;
  int delaybufsize_;
  int delaywritepos_;
  int delayreadpos_;
  SampleData delaybuf_; // predelay buffer
};

std::unique_ptr<TrackState>
Compressor::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<CompressorState>(channel_config);
}

void
Compressor::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
}

void
Compressor::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);
}
