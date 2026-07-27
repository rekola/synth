//
//  Created by Nigel Redmon on 11/24/12
//  EarLevel Engineering: earlevel.com
//  Copyright 2012 Nigel Redmon
//
//  For a complete explanation of the Biquad code:
//  http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
//
//  License:
//
//  This source code is provided as is, without warranty.
//  You may copy and distribute verbatim copies of this document.
//  You may modify and use this source code to create binary code
//  for your own purposes, free or commercial.
//

#ifndef _BIQUAD_H_
#define _BIQUAD_H_

#include "FilterType.h"

#include <cmath>

template <class T>
class Biquad {
public:
  explicit Biquad(FilterType type) : type_(type) {

  }

  explicit Biquad(FilterType type, T fc, T Q, T peakGainDB = 0)
    : type_(type),
      fc_(fc),
      QInv_(1 / Q),
      peakGain_(peakGainDB)
  {
    setup();
  }
  
  inline void apply(size_t blockSamples, float * buffer) {
    for (size_t i = 0; i < blockSamples; i++) {
      buffer[i] = process(buffer[i]);
    }
  }

  // Silence-only overload: advances the filter's internal state (z1_/z2_)
  // as if blockSamples zero samples had been processed, with no buffer
  // needed - for a channel that's absent this block but whose IIR history
  // should keep evolving/decaying rather than freezing and resuming later
  // as if no time had passed.
  inline void apply(size_t blockSamples) {
    for (size_t i = 0; i < blockSamples; i++) {
      process(0.0f);
    }
  }

  inline float process(float in) {
    T out = in * a0_ + z1_;
    z1_ = in * a1_ + z2_ - b1_ * out;
    z2_ = in * a2_ - b2_ * out;
    return (float)out;
  }

  void set(T fc, T Q, T peakGainDB = 0) {
    fc_ = fc;
    QInv_ = 1 / Q;
    peakGain_ = peakGainDB;
    
    setup();
  }

  void setFc(T fc) {
    fc_ = fc;
    
    setup();
  }
  
  bool active_ = false;
  
private:
  void setup() {
    T K = std::tan(pi * fc_);
    T KK = K * K;
    T norm, V;
    
    switch (type_) {
    case FilterType::lowpass:
      norm = 1 / (1 + K * QInv_ + KK);
      
      a0_ = KK * norm;
      a1_ = 2 * a0_;
      a2_ = a0_;
      b1_ = 2 * (KK - 1) * norm;
      b2_ = (1 - K * QInv_ + KK) * norm;
      break;

    case FilterType::highpass:
      norm = 1 / (1 + K * QInv_ + K * K);

      a0_ = 1 * norm;
      a1_ = -2 * a0_;
      a2_ = a0_;
      b1_ = 2 * (K * K - 1) * norm;
      b2_ = (1 - K * QInv_ + K * K) * norm;
      break;
            
    case FilterType::bandpass:
      norm = 1 / (1 + K * QInv_ + K * K);
      
      a0_ = K * QInv_ * norm;
      a1_ = 0;
      a2_ = -a0_;
      b1_ = 2 * (K * K - 1) * norm;
      b2_ = (1 - K * QInv_ + K * K) * norm;
      break;
            
    case FilterType::notch:
      norm = 1 / (1 + K * QInv_ + K * K);
      
      a0_ = (1 + K * K) * norm;
      a1_ = 2 * (K * K - 1) * norm;
      a2_ = a0_;
      b1_ = a1_;
      b2_ = (1 - K * QInv_ + K * K) * norm;
      break;
            
    case FilterType::peak:
      V = std::pow(10, std::fabs(peakGain_) / 20.0);
      
      if (peakGain_ >= 0) {    // boost
	norm = 1 / (1 + 1 * QInv_ * K + K * K);

	a0_ = (1 + V * QInv_ * K + K * K) * norm;
	a1_ = 2 * (K * K - 1) * norm;
	a2_ = (1 - V * QInv_ * K + K * K) * norm;
	b1_ = a1_;
	b2_ = (1 - 1 * QInv_ * K + K * K) * norm;
      } else {    // cut
	norm = 1 / (1 + V * QInv_ * K + K * K);

	a0_ = (1 + 1 * QInv_ * K + K * K) * norm;
	a1_ = 2 * (K * K - 1) * norm;
	a2_ = (1 - 1 * QInv_ * K + K * K) * norm;
	b1_ = a1_;
	b2_ = (1 - V * QInv_ * K + K * K) * norm;
      }
      break;
      
    case FilterType::lowshelf:
      V = std::pow(10, std::fabs(peakGain_) / 20.0);
      
      if (peakGain_ >= 0) {    // boost
	norm = 1 / (1 + sqrt_2 * K + K * K);
	
	a0_ = (1 + std::sqrt(2*V) * K + V * K * K) * norm;
	a1_ = 2 * (V * K * K - 1) * norm;
	a2_ = (1 - std::sqrt(2*V) * K + V * K * K) * norm;
	b1_ = 2 * (K * K - 1) * norm;
	b2_ = (1 - sqrt_2 * K + K * K) * norm;
      } else {    // cut
	norm = 1 / (1 + std::sqrt(2*V) * K + V * K * K);

	a0_ = (1 + sqrt_2 * K + K * K) * norm;
	a1_ = 2 * (K * K - 1) * norm;
	a2_ = (1 - sqrt_2 * K + K * K) * norm;
	b1_ = 2 * (V * K * K - 1) * norm;
	b2_ = (1 - std::sqrt(2*V) * K + V * K * K) * norm;
      }
      break;
      
    case FilterType::highshelf:
      V = std::pow(10, std::fabs(peakGain_) / 20.0);
      
      if (peakGain_ >= 0) {    // boost
	norm = 1 / (1 + sqrt_2 * K + K * K);

	a0_ = (V + std::sqrt(2*V) * K + K * K) * norm;
	a1_ = 2 * (K * K - V) * norm;
	a2_ = (V - std::sqrt(2*V) * K + K * K) * norm;
	b1_ = 2 * (K * K - 1) * norm;
	b2_ = (1 - sqrt_2 * K + K * K) * norm;
      } else {    // cut
	norm = 1 / (V + std::sqrt(2*V) * K + K * K);
	
	a0_ = (1 + sqrt_2 * K + K * K) * norm;
	a1_ = 2 * (K * K - 1) * norm;
	a2_ = (1 - sqrt_2 * K + K * K) * norm;
	b1_ = 2 * (K * K - V) * norm;
	b2_ = (V - std::sqrt(2*V) * K + K * K) * norm;
      }
      break;
    }
  }

  FilterType type_;
  T fc_ = 0, QInv_ = 0, peakGain_ = 0;

  T a0_, a1_, a2_, b1_, b2_;
  T z1_ = 0, z2_ = 0;

  static constexpr T pi = T(M_PI);
  static constexpr T sqrt_2 = std::sqrt(2);
};

#endif
