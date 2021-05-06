#include "HRFT.h"

#include <cstring>
#include <iostream>
#include <cassert>

#include <spatialaudio/AmbisonicEncoder.h>
#include <spatialaudio/AmbisonicBinauralizer.h>

using namespace std;

void
HRFT::initialize(size_t frames) {
  is_initialized = true;
      
  int order = 1;
  bool is_3d = true;
    
  myEncoder = make_shared<CAmbisonicEncoder>();
  // myEncoder->SetRoomRadius(10);
  if (!myEncoder->Configure(order, is_3d, 0)) {
    cerr << "encoder configure failed\n";
    exit(1);
  }
  
  myBinauralizer = make_shared<CAmbisonicBinauralizer>();
  unsigned tailLength;
  if (!myBinauralizer->Configure(order, is_3d, 44100, frames, tailLength, "")) { //./SADIE_KEMAR_DFC_256_order_fir_48000")) {
    // /home/rekola/src/personal/syna/D1_44K_24bit_256tap_FIR_SOFA.sofa")) {
    cerr << "decoder config failed\n";
    exit(1);
  }
  myBinauralizer->Refresh();
  
  left_buffer = std::unique_ptr<float[]>(new float[frames]);
  right_buffer = std::unique_ptr<float[]>(new float[frames]);
  
  myBFormat = make_shared<CBFormat>();
  if (!myBFormat->Configure(order, is_3d, frames)) {
    cerr << "format config failed\n";
    exit(1);
  }
}

void
HRFT::reset() {
  if (is_initialized) myBFormat->Reset();
}

void
HRFT::accumulate(const SampleData & data, float volume, float distance, float azimuth, float elevation) {
  assert(data.getChannels() == 1);
  
  size_t frames = data.size();
  const float * input = data.data();
  
  if (!is_initialized) initialize(frames);
  
  PolarPoint position;
  position.fDistance = distance;
  position.fAzimuth = azimuth * M_PI / 180.0f;
  position.fElevation = elevation * M_PI / 180.0f;

  myEncoder->SetPosition(position);
  myEncoder->Refresh();

  assert(frames == 1024);
  
  // memset(left, 0, frames * sizeof(float));
  // memset(right, 0, frames * sizeof(float));
  
  myEncoder->ProcessAccumul((float *)input, frames, myBFormat.get(), 0, volume);
}

void
HRFT::encode(SampleData & out, float master_volume) {
  float * output = out.data();
  size_t frames = out.size();
  
  if (!is_initialized) initialize(frames);
  
  float * buffers[2] = { left_buffer.get(), right_buffer.get() };
  
  myBinauralizer->Process(myBFormat.get(), buffers);

  float * left_data = right_buffer.get();
  float * right_data = left_buffer.get();

  for (size_t i = 0; i < frames; i++) {
    float l = left_data[i] * master_volume;
    float r = right_data[i] * master_volume;

    if (l > 1.0) l = 1.0;
    else if (l < -1.0) l = -1.0;

    if (r > 1.0) r = 1.0;
    else if (r < -1.0) r = -1.0;

    output[2 * i + 0] = l;
    output[2 * i + 1] = r;
  }
}
