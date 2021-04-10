#include "FFT.h"

#include "SampleData.h"
#include <cmath>
#include <memory>

using namespace std;

static inline bool IsPowerOfTwo( unsigned int p_nX ) {
  if (p_nX < 2) return false;
  if (p_nX & (p_nX-1)) return false;
  return true;
}

static inline unsigned int NumberOfBitsNeeded( unsigned int p_nSamples ) {
  int i;

  if (p_nSamples < 2) {
    return 0;
  }
  
  for (i = 0; ; i++) {
    if (p_nSamples & (1 << i)) return i;
  }
}

static inline unsigned int ReverseBits(unsigned int p_nIndex, unsigned int p_nBits) {
  unsigned int i, rev;
  
  for (i = rev = 0; i < p_nBits; i++) {
    rev = (rev << 1) | (p_nIndex & 1);
    p_nIndex >>= 1;
  }

  return rev;
}

static inline double Index_to_frequency(unsigned int p_nBaseFreq, unsigned int p_nSamples, unsigned int p_nIndex) {
  if(p_nIndex >= p_nSamples) {
    return 0.0;
  } else if(p_nIndex <= p_nSamples/2) {
      return ( (double)p_nIndex / (double)p_nSamples * p_nBaseFreq );
  } else {
    return ( -(double)(p_nSamples-p_nIndex) / (double)p_nSamples * p_nBaseFreq );
  }
}

static void fft_double(unsigned int p_nSamples, bool p_bInverseTransform, double *p_lpRealIn, double *p_lpImagIn, double *p_lpRealOut, double *p_lpImagOut) {
  if (!p_lpRealIn || !p_lpRealOut || !p_lpImagOut) return;
  
  unsigned int i, j, k, n;
  unsigned int BlockSize, BlockEnd;
  
  double angle_numerator = 2.0 * M_PI;
  double tr, ti;

  if (!IsPowerOfTwo(p_nSamples)) {
    return;
  }

  if (p_bInverseTransform) {
    angle_numerator = -angle_numerator;
  }

  unsigned int NumBits = NumberOfBitsNeeded ( p_nSamples );

  for (i = 0; i < p_nSamples; i++) {
    j = ReverseBits ( i, NumBits );
    p_lpRealOut[j] = p_lpRealIn[i];
    p_lpImagOut[j] = (p_lpImagIn == NULL) ? 0.0 : p_lpImagIn[i];
  }

  BlockEnd = 1;
  for (BlockSize = 2; BlockSize <= p_nSamples; BlockSize <<= 1 ) {
    double delta_angle = angle_numerator / (double)BlockSize;
    double sm2 = sin ( -2 * delta_angle );
    double sm1 = sin ( -delta_angle );
    double cm2 = cos ( -2 * delta_angle );
    double cm1 = cos ( -delta_angle );
    double w = 2 * cm1;
    double ar[3], ai[3];
    
    for (i=0; i < p_nSamples; i += BlockSize ) {
      ar[2] = cm2;
      ar[1] = cm1;
      
      ai[2] = sm2;
      ai[1] = sm1;
      
      for (j=i, n=0; n < BlockEnd; j++, n++) {
	ar[0] = w*ar[1] - ar[2];
	ar[2] = ar[1];
	ar[1] = ar[0];
	
	ai[0] = w*ai[1] - ai[2];
	ai[2] = ai[1];
	ai[1] = ai[0];
	
	k = j + BlockEnd;
	tr = ar[0]*p_lpRealOut[k] - ai[0]*p_lpImagOut[k];
	ti = ar[0]*p_lpImagOut[k] + ai[0]*p_lpRealOut[k];
	
	p_lpRealOut[k] = p_lpRealOut[j] - tr;
	p_lpImagOut[k] = p_lpImagOut[j] - ti;
	
	p_lpRealOut[j] += tr;
	p_lpImagOut[j] += ti;
	
      }
    }
    
    BlockEnd = BlockSize;
  }
  
  if (p_bInverseTransform) {
    double denom = (double)p_nSamples;
    
    for (i=0; i < p_nSamples; i++) {
      p_lpRealOut[i] /= denom;
      p_lpImagOut[i] /= denom;
    }
  }
}

#define mag_sqrd(re,im) (re*re+im*im)
#define Decibels(re,im) ((re == 0 && im == 0) ? (0) : 10.0 * log10(double(mag_sqrd(re,im))))
#define Amplitude(re,im,len) (GetFrequencyIntensity(re,im)/(len))
#define AmplitudeScaled(re,im,len,scale) ((int)Amplitude(re,im,len)%scale)

inline double GetFrequencyIntensity(double re, double im) {
  return sqrt((re*re)+(im*im));
}

vector<float>
FFT::perform(SampleData & input, size_t channel, size_t num_bins) {
  auto data = make_unique<double[]>(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    data[i] = input.data()[2 * i + channel];
  }

  bool inverse = false;
  auto real_output = make_unique<double[]>(2 * input.size());
  auto imag_output = make_unique<double[]>(2 * input.size());
  
  fft_double(input.size(), inverse, data.get(), 0, real_output.get(), imag_output.get());

  // float fmax = -99999.9f, fmin = 99999.9f;
  // vector<float> fft_output;
  vector<float> bins;
  for (size_t i = 0; i < num_bins; i++) bins.push_back(0);
  size_t actual_input_size = input.size() / 2;
  size_t bin_size = actual_input_size / num_bins;
  
  // data is mirrored, so only the left size
  for (size_t i = 1; i < actual_input_size; i++)	{
    float re = (float)real_output[i];
    float im = (float)imag_output[i];

    // float mag = AmplitudeScaled(re, im, actual_input_size, 256);
    float mag = Amplitude(re, im, actual_input_size);
    // float mag = mag_sqrd(re, im);
    // float mag = Decibels(re, im);

    size_t bin_i = i / bin_size;
    bins[bin_i] += mag;
  }

  return bins;
}
