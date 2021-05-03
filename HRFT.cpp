#include "HRFT.h"

#include <cstring>
#include <iostream>
#include <cassert>

#include <spatialaudio/AmbisonicEncoder.h>
#include <spatialaudio/AmbisonicBinauralizer.h>

using namespace std;

void
HRFT::filterAndMix(const float * input, float * output, size_t frames) {
  if (!is_initialized) {
    is_initialized = true;
    
    PolarPoint position;
    position.fElevation = elevation * M_PI / 180.0f;
    position.fAzimuth = azimuth * M_PI / 180.0f;
    position.fDistance = 5;

    int order = 1;
    bool is_3d = true;
    
    myEncoder = make_shared<CAmbisonicEncoder>();
    // myEncoder->SetRoomRadius(10);
    if (!myEncoder->Configure(order, is_3d, 0)) {
      cerr << "encoder configure failed\n";
      exit(1);
    }
    myEncoder->SetPosition(position);
    myEncoder->Refresh();

    myBinauralizer = make_shared<CAmbisonicBinauralizer>();
    unsigned tailLength;
    if (!myBinauralizer->Configure(order, is_3d, 44100, frames, tailLength, "")) { //./SADIE_KEMAR_DFC_256_order_fir_48000")) {
      // /home/rekola/src/personal/syna/D1_44K_24bit_256tap_FIR_SOFA.sofa")) {
      cerr << "decoder config failed\n";
      exit(1);
    }
    myBinauralizer->Refresh();
    
    buffers[0] = new float[frames];
    buffers[1] = new float[frames];

    myBFormat = make_shared<CBFormat>();
    if (!myBFormat->Configure(order, is_3d, frames)) {
      cerr << "format config failed\n";
      exit(1);
    }
  }

  assert(frames == 1024);
  
  assert(myEncoder.get());
  assert(myBinauralizer.get());

  float * left = buffers[0];
  float * right = buffers[1];

  // memset(left, 0, frames * sizeof(float));
  // memset(right, 0, frames * sizeof(float));
  // myBFormat->Reset();
  
  myEncoder->Process((float *)input, frames, myBFormat.get());
  myBinauralizer->Process(myBFormat.get(), buffers);

  for (size_t i = 0; i < frames; i++) {
    output[2 * i + 0] += left[i];
    output[2 * i + 1] += right[i];
  }
}
