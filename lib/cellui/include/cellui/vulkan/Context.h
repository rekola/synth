#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cellui::vulkan {

// What one Vulkan device offers the renderer.
struct DeviceInfo {
  enum class Type { OTHER, INTEGRATED, DISCRETE, VIRTUAL, CPU };

  std::string name;
  Type type = Type::OTHER;
  uint32_t api_version = 0;  // VK_MAKE_API_VERSION
  // It can run the renderer: Vulkan 1.3 with dynamic rendering, a graphics
  // queue, and FP16 colour targets that can be blended into and read back.
  bool usable = false;
  std::string reason;  // why not, when it is not
  // Images can be exported as dma-bufs with a format modifier, which is how
  // a toolkit that has no Vulkan widget is handed a frame.
  bool dmabuf_export = false;
  // The device can tell a display about the HDR metadata of its content.
  bool hdr_metadata = false;

  bool hardware() const { return type != Type::CPU; }
};

// What the machine has, found without showing anything.
struct Capabilities {
  bool loader = false;  // a Vulkan loader and an instance
  std::string error;    // why not, when there is none
  uint32_t instance_version = 0;
  // A swap chain can be given an HDR colour space (VK_EXT_swapchain_colorspace).
  bool swapchain_colorspace = false;
  std::vector<DeviceInfo> devices;

  // The device the renderer would use: the best usable one, a CPU rasteriser
  // only when `allow_cpu`. Null when there is none.
  const DeviceInfo * best(bool allow_cpu) const;
};

Capabilities query();

// A device and what it takes to draw into an FP16 target: the thin layer the
// renderer is built on. Every call that can fail says why.
class Context {
 public:
  ~Context();
  static std::unique_ptr<Context> create(bool allow_cpu, std::string * error);

  const DeviceInfo & device() const;

  // Fills a width x height FP16 RGBA target through the shader pipeline: from
  // zero, each colour drawn over all of it in turn with additive blending
  // (so values above 1 add up, as bloom needs), then reads the target back as
  // float RGBA, row 0 first.
  bool renderAdditive(int width, int height, const std::vector<std::array<float, 4>> & colours, std::vector<float> & rgba,
                      std::string * error);

 private:
  Context();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cellui::vulkan
