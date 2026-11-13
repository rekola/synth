#include "TestFramework.h"

#include <cellui/StageStats.h>

#include <string>

using namespace cellui;

// A stage's time is counted only while the stats are switched on, and to the stage that was named.
TEST(stage_timers_count_cpu_time_only_while_switched_on) {
  auto burn = [] {
    volatile double x = 0;
    for (int i = 0; i < 2000000; i++) x = x + i * 0.5;
  };
  StageStats::enable(false);
  auto before = StageStats::totals();
  {
    StageTimer t(Stage::DEFLATE);
    burn();
  }
  CHECK(StageStats::totals() == before);
  StageStats::enable(true);
  {
    StageTimer t(Stage::DEFLATE);
    burn();
  }
  auto after = StageStats::totals();
  CHECK(after[static_cast<size_t>(Stage::DEFLATE)] > before[static_cast<size_t>(Stage::DEFLATE)]);
  CHECK(after[static_cast<size_t>(Stage::BASE64)] == before[static_cast<size_t>(Stage::BASE64)]);
  StageStats::enable(false);
  CHECK(std::string(stageName(Stage::SIXEL_MAP)) == "sixel_map");
}
