#include <cellui/vulkan/Context.h>

#include <cstdio>

// Prints what Vulkan offers on this machine, so a renderer's choices can be
// checked on real hardware.
int main() {
  using namespace cellui::vulkan;
  Capabilities caps = query();
  if (!caps.loader) {
    std::printf("no Vulkan: %s\n", caps.error.c_str());
    return 1;
  }
  std::printf("instance %u.%u, swap chain colour spaces: %s\n", caps.instance_version >> 22, (caps.instance_version >> 12) & 0x3ff,
              caps.swapchain_colorspace ? "yes" : "no");
  for (const DeviceInfo & d : caps.devices) {
    std::printf("%s (%s): %s%s%s; dma-buf export %s, HDR metadata %s\n", d.name.c_str(), d.hardware() ? "hardware" : "CPU",
                d.usable ? "usable" : "unusable", d.usable ? "" : ": ", d.reason.c_str(), d.dmabuf_export ? "yes" : "no",
                d.hdr_metadata ? "yes" : "no");
  }
  const DeviceInfo * best = caps.best(false);
  std::printf("would use: %s\n", best ? best->name.c_str() : "none (the terminal or window backend)");
  return 0;
}
