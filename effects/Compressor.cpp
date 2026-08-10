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
#include <vector>

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
  if (x < linearthreshold) {
    return x;
  }
  if (knee <= 0.0f) {// no knee in curve
    return db2lin(threshold + slope * (lin2db(x) - threshold));
  }
  if (x < linearthresholdknee) {
    return kneecurve(x, k, linearthreshold);
  }
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

static inline float maxf(float v1, float v2) {
  return v1 > v2 ? v1 : v2;
}   

static inline float fixf(float v, float def) {
  // fix NaN and infinity values that sneak in... not sure why this is needed, but it is
  if (std::isnan(v) || std::isinf(v) || v > 24000000) return def;
  return v;
}

using namespace std;

class CompressorState : public EffectState {
public:  
  CompressorState(const ChannelConfiguration & channel_config,
		  float pregain,
		  float threshold,
		  float knee,
		  float ratio,
		  float attack,
		  float release,
		  float predelay,
		  float releasezone1,
		  float releasezone2,
		  float releasezone3,
		  float releasezone4,
		  float postgain,
		  float wet
		  )
    : EffectState(channel_config)
  {
    // the main initialization
    // it does a bunch of pre-calculation so that the inner loop of signal processing is fast
    
    auto rate = getChannelConfiguration().getAudioOutSampleRate();
    
    // setup the predelay buffer
    delaybufsize_ = static_cast<int>(rate * predelay);
    if (delaybufsize_ < 1) {
      delaybufsize_ = 1;
    } else if (delaybufsize_ > SF_COMPRESSOR_MAXDELAY) {
      delaybufsize_ = SF_COMPRESSOR_MAXDELAY;
    }
    // +2 headroom for AuxA/AuxB - applyEffect() applies the compressor's
    // gain to them too (see its own doc comment), so the delay line needs
    // to carry them alongside the regular channels; raw channel count is
    // all this internal scratch buffer needs, no Aux bookkeeping of its
    // own (plain getChannelData() access only, never hasChannel()/
    // getChannel()).
    delaybuf_ = AudioBuffer(static_cast<short>(getChannelConfiguration().numberOfChannels() + 2), delaybufsize_);
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
    
    // metering values (not used in core algorithm, but used to output a meter if desired)
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
    dry_                  = 1.0f - wet;
    k_                    = k;
    kneedboffset_         = kneedboffset;
    linearthresholdknee_  = linearthresholdknee;
    mastergain_           = mastergain;
    a_                    = a;
    b_                    = b;
    c_                    = c;
    d_                    = d;

    delaywritepos_        = 0;
    delayreadpos_         = delaybufsize_ > 1 ? 1 : 0;
  }

  void applyEffect(AudioBuffer & input) override {
    // The gain-reduction algorithm computes a single scalar `gain` per
    // sample and applies it uniformly to every channel below, Main and
    // Aux alike - a compressed instrument should send its already-
    // compressed dynamics to the reverb/delay bus too, the same reasoning
    // as EnvelopeFilter/Amplifier (unlike a tone-shaping effect, where the
    // send stays untouched). Only the *detection* step (computing
    // `inputmax`/`gain` itself) reads Main channels only - AuxA/AuxB
    // shouldn't be able to influence how hard the compressor squeezes.
    int num_channels = input.numberOfChannels();
    int main_channels = input.regularChannelCount();
    bool compressor_active = false;
    if (num_channels > 0) {
	    std::vector<float *> delaybuf(num_channels), in(num_channels);
	    for (int c = 0; c < num_channels; c++) {
	      delaybuf[c] = delaybuf_.getChannelData(c);
	      in[c] = input.getChannelData(c);
	    }

	    constexpr float ang90 = (float)M_PI * 0.5f;
	    constexpr float ang90inv = 2.0f / (float)M_PI;

	    int samplesperchunk = SF_COMPRESSOR_SPU;
	    int chunks = input.numberOfFrames() / samplesperchunk;
	    int samplepos = 0;

	    for (int ch = 0; ch < chunks; ch++) {
	      detectoravg_ = fixf(detectoravg_, 1.0f);

	      float desiredgain = detectoravg_;
	      float scaleddesiredgain = asinf(desiredgain) * ang90inv;
	      float compdiffdb = lin2db(compgain_ / scaleddesiredgain);

	      // calculate envelope rate based on whether we're attacking or releasing
	      float enveloperate;
	      if (compdiffdb < 0.0f) { // compgain_ < scaleddesiredgain, so we're releasing
		compressor_active = true;
	
		compdiffdb = fixf(compdiffdb, -1.0f);
		maxcompdiffdb_ = -1; // reset for a future attack mode
		// apply the adaptive release curve
		// scale compdiffdb between 0-3
		float x = (clampf(compdiffdb, -12.0f, 0.0f) + 12.0f) * 0.25f;
		float releasesamples = adaptivereleasecurve(x, a_, b_, c_, d_);
		enveloperate = db2lin(SF_COMPRESSOR_SPACINGDB / releasesamples);
	      } else { // compresorgain > scaleddesiredgain, so we're attacking
		compdiffdb = fixf(compdiffdb, 1.0f);
		if (maxcompdiffdb_ == -1 || maxcompdiffdb_ < compdiffdb) {
		  maxcompdiffdb_ = compdiffdb;
		}
		float attenuate = maxcompdiffdb_;
		if (attenuate < 0.5f) {
		  attenuate = 0.5f;
		}
		enveloperate = 1.0f - powf(0.25f / attenuate, attacksamplesinv_);
	      }

	      if ((ch + 1) * samplesperchunk > input.numberOfFrames()) {
		samplesperchunk = input.numberOfFrames() * ch;
	      }

	      // process the chunk
	      for (int chi = 0; chi < samplesperchunk; chi++, samplepos++,
		     delayreadpos_ = (delayreadpos_ + 1) % delaybufsize_,
		     delaywritepos_ = (delaywritepos_ + 1) % delaybufsize_) {

		float inputmax = 0.0f;
		for (int c = 0; c < num_channels; c++) {
		  auto v = in[c][samplepos] * linearpregain_;
		  delaybuf[c][delaywritepos_] = v;
		  if (c < main_channels) inputmax = maxf(inputmax, absf(v));
		}

		float attenuation;
		if (inputmax < 0.0001f) {
		  attenuation = 1.0f;
		} else{
		  float inputcomp = compcurve(inputmax, k_, slope_, linearthreshold_,
					      linearthresholdknee_, threshold_, knee_, kneedboffset_);
		  attenuation = inputcomp / inputmax;	  
		}

		float rate;
		if (attenuation > detectoravg_) { // if releasing
		  float attenuationdb = -lin2db(attenuation);
		  if (attenuationdb < 2.0f) {
		    attenuationdb = 2.0f;
		  }
		  float dbpersample = attenuationdb * satreleasesamplesinv_;
		  rate = db2lin(dbpersample) - 1.0f;
		} else {
		  rate = 1.0f;
		}
	
		detectoravg_ += (attenuation - detectoravg_) * rate;
		if (detectoravg_ > 1.0f) {
		  detectoravg_ = 1.0f;
		}
		detectoravg_ = fixf(detectoravg_, 1.0f);
	
		if (enveloperate < 1) { // attack, reduce gain
		  compgain_ += (scaleddesiredgain - compgain_) * enveloperate;
		} else { // release, increase gain
		  compgain_ *= enveloperate;
		  if (compgain_ > 1.0f) {
		    compgain_ = 1.0f;
		  }
		}

		// the final gain value!
		float premixgain = sinf(ang90 * compgain_);
		float gain = dry_ + wet_ * mastergain_ * premixgain;
	
		// calculate metering (not used in core algo, but used to output a meter if desired)
		float premixgaindb = lin2db(premixgain);
		if (premixgaindb < metergain_) {
		  metergain_ = premixgaindb; // spike immediately
		} else {
		  metergain_ += (premixgaindb - metergain_) * meterrelease_; // fall slowly
		}

		// apply the gain
		for (int c = 0; c < num_channels; c++) {
		  in[c][samplepos] = delaybuf[c][delayreadpos_] * gain;
		}
	      }
	    }
    }

    // "Active" tracks whether there was anything to apply gain to at all
    // (Main, Aux, or both) - not Main specifically, since an Aux-only
    // input (Send Main = 0) still genuinely gets processed above, even
    // though detection itself (compressor_active, below) stays Main-only.
    setEffectActive(num_channels > 0);
    setTrackInfo(TrackInfo( compressor_active, input.isClipping(), metergain_ ));
  }
  
private:
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
  float detectoravg_ = 0.0f;
  float compgain_ = 1.0f;
  float maxcompdiffdb_ = -1.0f;
  int delaybufsize_;
  int delaywritepos_;
  int delayreadpos_;
  AudioBuffer delaybuf_; // predelay buffer
};

std::unique_ptr<TrackState>
Compressor::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<CompressorState>(channel_config,
				      pregain_,
				      threshold_,
				      knee_,
				      ratio_,
				      0.003f, // attack
				      0.250f, // release
				      0.006f, // predelay
				      0.090f, // releasezone1
				      0.160f, // releasezone2
				      0.420f, // releasezone3
				      0.980f, // releasezone4
				      postgain_,
				      1.000f  // wet
				      );
}

void
Compressor::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  pregain_ = input.getFloat("pregain", 0.0f);
  threshold_ = input.getFloat("threshold", -24.0f);
  knee_ = input.getFloat("knee", 30.0f);
  ratio_ = input.getFloat("ratio", 12.0f);
  postgain_ = input.getFloat("postgain", 0.0f);
}

void
Compressor::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("pregain", pregain_);
  output.set("threshold", threshold_);
  output.set("knee", knee_);
  output.set("ratio", ratio_);
  output.set("postgain", postgain_);
}
