#include "TestFramework.h"

#include <cellui/vulkan/Context.h>

#include <cstdio>

using namespace cellui::vulkan;

namespace {
// A machine with no driver (CI) skips; a CPU rasteriser is enough to run the
// pipeline, though it is never what the renderer would pick.
std::unique_ptr<Context> makeContext() {
  std::string error;
  auto context = Context::create(true, &error);
  if (!context) std::printf("SKIP: no Vulkan device (%s)\n", error.c_str());
  return context;
}
}  // namespace

TEST(vulkan_query_reports_a_loader_or_says_why_not) {
  Capabilities caps = query();
  CHECK(caps.loader || !caps.error.empty());
  if (const DeviceInfo * d = caps.best(true)) CHECK(d->usable);
  // Without CPU rasterisers allowed, the best device is a real one.
  if (const DeviceInfo * d = caps.best(false)) CHECK(d->hardware());
}

TEST(vulkan_fp16_target_keeps_values_above_one) {
  auto context = makeContext();
  if (!context) return;
  std::vector<float> rgba;
  std::string error;
  CHECK(context->renderAdditive(5, 3, {{6.0f, 0.5f, 0.0f, 1.0f}}, rgba, &error));
  CHECK(rgba.size() == 5 * 3 * 4);
  for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
    CHECK_NEAR(rgba[i], 6.0f, 1e-3f);
    CHECK_NEAR(rgba[i + 1], 0.5f, 1e-3f);
    CHECK_NEAR(rgba[i + 2], 0.0f, 1e-3f);
  }
}

TEST(vulkan_additive_blend_sums_light) {
  auto context = makeContext();
  if (!context) return;
  std::vector<float> rgba;
  std::string error;
  CHECK(context->renderAdditive(4, 4, {{6.0f, 0.0f, 0.25f, 0.0f}, {6.0f, 1.0f, 0.25f, 0.0f}}, rgba, &error));
  CHECK_NEAR(rgba[0], 12.0f, 1e-2f);
  CHECK_NEAR(rgba[1], 1.0f, 1e-3f);
  CHECK_NEAR(rgba[2], 0.5f, 1e-3f);
}

TEST(vulkan_cpu_rasteriser_is_not_hardware) {
  Capabilities caps = query();
  for (const DeviceInfo & d : caps.devices)
    if (d.type == DeviceInfo::Type::CPU) CHECK(!d.hardware());
}
