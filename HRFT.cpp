#include "HRFT.h"

#include <cstring>
#include <iostream>
#include <cassert>

#include <spatialaudio/AmbisonicEncoder.h>
#include <spatialaudio/AmbisonicBinauralizer.h>

using namespace std;

static const char * get_filename(unsigned int outSampleRate) {
  switch (outSampleRate) {
  case 44100: return "/home/rekola/src/personal/syna/build/data/D1_44K_16bit_256tap_FIR_SOFA.sofa";
  case 48000: return "/home/rekola/src/personal/syna/build/data/D1_48K_24bit_256tap_FIR_SOFA.sofa";
  case 96000: return "/home/rekola/src/personal/syna/build/data/D1_96K_24bit_512tap_FIR_SOFA.sofa";
  }
  return "";
}

HRFT::HRFT(unsigned int _outSampleRate) : Mixer(_outSampleRate) {
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
  
  if (!myBinauralizer->Configure(order, is_3d, getOutSampleRate(), frames, tailLength, get_filename(_outSampleRate))) {
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
  myBFormat->Reset();
}

void
HRFT::accumulate(const SampleData & data, float volume, float distance, float azimuth, float elevation) {
  assert(data.getChannels() == 1);
  assert(data.size() == frames);
  
  const float * input = data.data();
  
  PolarPoint position;
  position.fDistance = distance;
  position.fAzimuth = azimuth * M_PI / 180.0f;
  position.fElevation = elevation * M_PI / 180.0f;

  myEncoder->SetPosition(position);
  myEncoder->Refresh();    
  myEncoder->ProcessAccumul(const_cast<float *>(input), frames, myBFormat.get(), 0, volume);
}

SampleData
HRFT::encode(float master_volume) {
  SampleData out(2, frames);
  float * output = out.data();
  
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

  return out;
}
