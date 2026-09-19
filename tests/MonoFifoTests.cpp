#include "TestFramework.h"

#include "../src/dsp/MonoFifo.h"

#include <vector>

TEST(mono_fifo_pulls_what_was_pushed_in_order) {
  dsp::MonoFifo fifo;
  fifo.setCapacity(8);
  float in[] = { 1, 2, 3 };
  fifo.push(in, 3);
  float out[3] = {};
  fifo.pull(out, 3);
  CHECK(out[0] == 1.0f && out[1] == 2.0f && out[2] == 3.0f);
  CHECK(fifo.size() == 0);
}

TEST(mono_fifo_fills_an_underrun_with_silence) {
  dsp::MonoFifo fifo;
  fifo.setCapacity(8);
  float in[] = { 5, 6 };
  fifo.push(in, 2);
  float out[4] = { 9, 9, 9, 9 };
  fifo.pull(out, 4);
  CHECK(out[0] == 5.0f && out[1] == 6.0f && out[2] == 0.0f && out[3] == 0.0f);
}

TEST(mono_fifo_drops_the_oldest_on_overflow_and_trim) {
  dsp::MonoFifo fifo;
  fifo.setCapacity(4);
  float in[] = { 1, 2, 3, 4, 5, 6 };
  fifo.push(in, 6); // wraps: 1 and 2 are dropped
  CHECK(fifo.size() == 4);
  fifo.trimTo(2); // 3 and 4 dropped
  float out[2] = {};
  fifo.pull(out, 2);
  CHECK(out[0] == 5.0f && out[1] == 6.0f);
}

TEST(mono_fifo_without_capacity_stays_empty) {
  dsp::MonoFifo fifo;
  float in[] = { 1 };
  fifo.push(in, 1);
  float out[1] = { 9 };
  fifo.pull(out, 1);
  CHECK(fifo.size() == 0 && out[0] == 0.0f);
}
