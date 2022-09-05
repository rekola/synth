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

#ifndef _BIQUADFILTER_H_
#define _BIQUADFILTER_H_

#include "FilterType.h"

#include <cmath>

template <class T>
class BiquadFilter {
public:
  BiquadFilter(FilterType type) : type_(type) {

  }

  void setup(float Fc) {
    T K = std::tan(pi * Fc);
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

  float process(float in) {
    T out = in * a0_ + z1_;
    z1_ = in * a1_ + z2_ - b1_ * out;
    z2_ = in * a2_ - b2_ * out;
    return (float)out;
  }

  void reset() {
    z1_ = z2_ = 0;
  }
  
  T QInv_ = 0;
  bool active_ = false;

private:
  FilterType type_;
  T a0_, a1_, a2_, b1_, b2_;
  T z1_ = 0, z2_ = 0;
  T peakGain_ = 0;

  static constexpr T pi = T(M_PI);
  static constexpr T sqrt_2 = std::sqrt(2);
};

#endif
