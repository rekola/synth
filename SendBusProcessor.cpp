#include "SendBusProcessor.h"

SendBusProcessor::SendBusProcessor(int sampleRate)
  : chorus_(2, sampleRate, /*voices=*/3, /*rateHz=*/0.5f, /*centerDelayMs=*/15.0f, /*depthMs=*/4.0f, /*decorrelate=*/true) {
  // Reasonable fixed reverb sound - the same default timbre Reverb.h's own
  // member initializers use when no preset is applied - except MIX, which
  // is deliberately fully wet here (see the header comment).
  reverb_.setParameter(MVerb<float>::DAMPINGFREQ, 0.9f);
  reverb_.setParameter(MVerb<float>::DENSITY, 0.0f);
  reverb_.setParameter(MVerb<float>::BANDWIDTHFREQ, 0.9f);
  reverb_.setParameter(MVerb<float>::DECAY, 0.5f);
  reverb_.setParameter(MVerb<float>::PREDELAY, 0.0f);
  reverb_.setParameter(MVerb<float>::SIZE, 1.0f);
  reverb_.setParameter(MVerb<float>::GAIN, 1.0f);
  reverb_.setParameter(MVerb<float>::MIX, 1.0f);
  reverb_.setParameter(MVerb<float>::EARLYMIX, 1.0f);
  reverb_.setSampleRate(static_cast<float>(sampleRate));

  chorus_.setMix(1.0f);
}

SampleData
SendBusProcessor::process(const SampleData & send_a_mono, const SampleData & send_b_mono, int frames) {
  // Reverb: MVerb is hardcoded 2-in/2-out - duplicate the mono SendA sum
  // into both inputs (same duplicate-mono-to-stereo pattern ReverbState
  // already uses for its own mono-input case), keeping *both* outputs this
  // time for a genuine stereo spread (ReverbState's own mono path only
  // writes back the left channel - a quirk local to that class, not
  // repeated here).
  auto a = const_cast<float *>(send_a_mono.getChannelData(0));
  float * reverb_in[2] = { a, a };

  // MVerb doesn't reliably overwrite every output sample itself (see
  // ReverbState::applyEffect(), effects/Reverb.cpp, which explicitly
  // memsets its own output buffers before calling process() for the same
  // reason) - reverb_out must be zeroed first, since SampleData's
  // raw-count constructor uses aligned_alloc (uninitialized memory), not
  // calloc.
  SampleData reverb_out(2, frames);
  reverb_out.zero();
  float * reverb_out_ptr[2] = { reverb_out.getChannelData(0), reverb_out.getChannelData(1) };
  reverb_.process(reverb_in, reverb_out_ptr, frames);

  // Chorus: duplicate the mono SendB sum into 2 identical channels, then
  // let the decorrelate = true engine synthesize width from there.
  SampleData chorus_out(2, frames);
  auto b = send_b_mono.getChannelData(0);
  auto cl = chorus_out.getChannelData(0), cr = chorus_out.getChannelData(1);
  for (int i = 0; i < frames; i++) {
    cl[i] = b[i];
    cr[i] = b[i];
  }
  chorus_.process(chorus_out);

  SampleData wet(2, frames);
  auto wl = wet.getChannelData(0), wr = wet.getChannelData(1);
  auto rl = reverb_out.getChannelData(0), rr = reverb_out.getChannelData(1);
  for (int i = 0; i < frames; i++) {
    wl[i] = kWetTrim * (rl[i] + cl[i]);
    wr[i] = kWetTrim * (rr[i] + cr[i]);
  }
  wet.setNonZero();

  return wet;
}
