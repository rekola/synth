#include <cellui/vulkan/Context.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// SPIR-V compiled from shaders/ at build time (glslang's C header form).
#include "solid.frag.h"
#include "solid.vert.h"

namespace cellui::vulkan {

namespace {

constexpr VkFormat kTargetFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

bool hasExtension(const std::vector<VkExtensionProperties> & list, const char * name) {
  for (const auto & e : list)
    if (std::strcmp(e.extensionName, name) == 0) return true;
  return false;
}

DeviceInfo::Type typeOf(VkPhysicalDeviceType t) {
  switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return DeviceInfo::Type::INTEGRATED;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return DeviceInfo::Type::DISCRETE;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return DeviceInfo::Type::VIRTUAL;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return DeviceInfo::Type::CPU;
    default: return DeviceInfo::Type::OTHER;
  }
}

int rank(DeviceInfo::Type t) {
  switch (t) {
    case DeviceInfo::Type::DISCRETE: return 4;
    case DeviceInfo::Type::INTEGRATED: return 3;
    case DeviceInfo::Type::VIRTUAL: return 2;
    case DeviceInfo::Type::OTHER: return 1;
    case DeviceInfo::Type::CPU: return 0;
  }
  return 0;
}

std::string failure(const char * what, VkResult r) { return std::string(what) + " failed (VkResult " + std::to_string(static_cast<int>(r)) + ")"; }

// An instance, and the device that is to be used on it.
struct Candidate {
  VkPhysicalDevice device = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  DeviceInfo info;
};

bool createInstance(VkInstance & instance, uint32_t & version, bool & colorspace, std::string * error) {
  version = VK_API_VERSION_1_0;
  if (auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion")))
    enumerate(&version);
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
  colorspace = hasExtension(extensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "cellui";
  app.apiVersion = std::min(version, VK_API_VERSION_1_3);
  VkInstanceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.pApplicationInfo = &app;
  VkResult r = vkCreateInstance(&info, nullptr, &instance);
  if (r != VK_SUCCESS) {
    if (error) *error = failure("vkCreateInstance", r);
    return false;
  }
  return true;
}

std::vector<Candidate> gather(VkInstance instance) {
  std::vector<Candidate> out;
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance, &count, devices.data());
  for (VkPhysicalDevice pd : devices) {
    Candidate c;
    c.device = pd;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    c.info.name = props.deviceName;
    c.info.type = typeOf(props.deviceType);
    c.info.api_version = props.apiVersion;

    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> extensions(ext_count);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, extensions.data());
    c.info.dmabuf_export = hasExtension(extensions, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME) &&
                           hasExtension(extensions, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    c.info.hdr_metadata = hasExtension(extensions, VK_EXT_HDR_METADATA_EXTENSION_NAME);

    if (props.apiVersion < VK_API_VERSION_1_3) {
      c.info.reason = "Vulkan 1.3 is needed";
    } else {
      VkPhysicalDeviceVulkan13Features f13{};
      f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
      VkPhysicalDeviceFeatures2 features{};
      features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
      features.pNext = &f13;
      vkGetPhysicalDeviceFeatures2(pd, &features);
      VkFormatProperties format;
      vkGetPhysicalDeviceFormatProperties(pd, kTargetFormat, &format);
      const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
      uint32_t families = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(pd, &families, nullptr);
      std::vector<VkQueueFamilyProperties> queues(families);
      vkGetPhysicalDeviceQueueFamilyProperties(pd, &families, queues.data());
      bool found = false;
      for (uint32_t i = 0; i < families && !found; i++) {
        if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
          c.queue_family = i;
          found = true;
        }
      }
      if (!f13.dynamicRendering) c.info.reason = "no dynamic rendering";
      else if ((format.optimalTilingFeatures & need) != need) c.info.reason = "FP16 colour targets cannot be blended into and read back";
      else if (!found) c.info.reason = "no graphics queue";
      else c.info.usable = true;
    }
    out.push_back(std::move(c));
  }
  return out;
}

const Candidate * pick(const std::vector<Candidate> & all, bool allow_cpu) {
  const Candidate * best = nullptr;
  for (const Candidate & c : all) {
    if (!c.info.usable || (!allow_cpu && !c.info.hardware())) continue;
    if (!best || rank(c.info.type) > rank(best->info.type)) best = &c;
  }
  return best;
}

float halfToFloat(uint16_t h) {
  uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1fu, mantissa = h & 0x3ffu;
  float value;
  if (exponent == 0) value = std::ldexp(static_cast<float>(mantissa), -24);
  else if (exponent == 31) value = mantissa ? std::nanf("") : INFINITY;
  else value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
  return sign ? -value : value;
}

}  // namespace

const DeviceInfo * Capabilities::best(bool allow_cpu) const {
  const DeviceInfo * best = nullptr;
  for (const DeviceInfo & d : devices) {
    if (!d.usable || (!allow_cpu && !d.hardware())) continue;
    if (!best || rank(d.type) > rank(best->type)) best = &d;
  }
  return best;
}

Capabilities query() {
  Capabilities caps;
  VkInstance instance = VK_NULL_HANDLE;
  if (!createInstance(instance, caps.instance_version, caps.swapchain_colorspace, &caps.error)) return caps;
  caps.loader = true;
  for (Candidate & c : gather(instance)) caps.devices.push_back(std::move(c.info));
  vkDestroyInstance(instance, nullptr);
  return caps;
}

struct Context::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
  DeviceInfo info;
  uint32_t queue_family = 0;

  ~Impl() {
    if (device) {
      vkDeviceWaitIdle(device);
      if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
      if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
      if (vert) vkDestroyShaderModule(device, vert, nullptr);
      if (frag) vkDestroyShaderModule(device, frag, nullptr);
      if (pool) vkDestroyCommandPool(device, pool, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
  }

  bool memoryType(uint32_t bits, VkMemoryPropertyFlags want, uint32_t & index) const {
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; i++) {
      if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & want) == want) {
        index = i;
        return true;
      }
    }
    return false;
  }

  bool shader(const uint32_t * code, size_t bytes, VkShaderModule & module, std::string * error) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = bytes;
    info.pCode = code;
    VkResult r = vkCreateShaderModule(device, &info, nullptr, &module);
    if (r != VK_SUCCESS) {
      if (error) *error = failure("vkCreateShaderModule", r);
      return false;
    }
    return true;
  }

  bool buildPipeline(std::string * error) {
    if (!shader(solid_vert_spv, sizeof solid_vert_spv, vert, error) || !shader(solid_frag_spv, sizeof solid_frag_spv, frag, error)) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4};
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    VkResult r = vkCreatePipelineLayout(device, &layout_info, nullptr, &layout);
    if (r != VK_SUCCESS) {
      if (error) *error = failure("vkCreatePipelineLayout", r);
      return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkFormat format = kTargetFormat;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState = &viewport;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = layout;
    r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
    if (r != VK_SUCCESS) {
      if (error) *error = failure("vkCreateGraphicsPipelines", r);
      return false;
    }
    return true;
  }
};

Context::Context() : impl_(new Impl) { }
Context::~Context() = default;

const DeviceInfo & Context::device() const { return impl_->info; }

std::unique_ptr<Context> Context::create(bool allow_cpu, std::string * error) {
  std::unique_ptr<Context> context(new Context);
  Impl & s = *context->impl_;
  uint32_t version = 0;
  bool colorspace = false;
  if (!createInstance(s.instance, version, colorspace, error)) return nullptr;
  auto candidates = gather(s.instance);
  const Candidate * chosen = pick(candidates, allow_cpu);
  if (!chosen) {
    if (error) {
      *error = candidates.empty() ? "no Vulkan device" : "no usable " + std::string(allow_cpu ? "" : "hardware ") + "Vulkan device";
      for (const Candidate & c : candidates) *error += "; " + c.info.name + ": " + (c.info.usable ? "a CPU rasteriser" : c.info.reason);
    }
    return nullptr;
  }
  s.physical = chosen->device;
  s.queue_family = chosen->queue_family;
  s.info = chosen->info;

  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = s.queue_family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkPhysicalDeviceVulkan13Features f13{};
  f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  f13.dynamicRendering = VK_TRUE;
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.pNext = &f13;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  VkResult r = vkCreateDevice(s.physical, &device_info, nullptr, &s.device);
  if (r != VK_SUCCESS) {
    if (error) *error = failure("vkCreateDevice", r);
    return nullptr;
  }
  vkGetDeviceQueue(s.device, s.queue_family, 0, &s.queue);
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = s.queue_family;
  r = vkCreateCommandPool(s.device, &pool_info, nullptr, &s.pool);
  if (r != VK_SUCCESS) {
    if (error) *error = failure("vkCreateCommandPool", r);
    return nullptr;
  }
  if (!s.buildPipeline(error)) return nullptr;
  return context;
}

bool Context::renderAdditive(int width, int height, const std::vector<std::array<float, 4>> & colours, std::vector<float> & rgba,
                             std::string * error) {
  Impl & s = *impl_;
  auto fail = [&](const std::string & why) {
    if (error) *error = why;
    return false;
  };
  if (width <= 0 || height <= 0) return fail("an empty target");
  const VkDeviceSize pixels = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height);
  const VkDeviceSize bytes = pixels * 8;  // four halves

  // The target, its view, and a host-visible buffer to read it back into.
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory image_memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkCommandBuffer commands = VK_NULL_HANDLE;
  bool ok = false;
  std::string why;
  do {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = kTargetFormat;
    image_info.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    image_info.mipLevels = image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(s.device, &image_info, nullptr, &image);
    if (r != VK_SUCCESS) { why = failure("vkCreateImage", r); break; }
    VkMemoryRequirements need;
    vkGetImageMemoryRequirements(s.device, image, &need);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = need.size;
    if (!s.memoryType(need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, alloc.memoryTypeIndex) &&
        !s.memoryType(need.memoryTypeBits, 0, alloc.memoryTypeIndex)) { why = "no memory for the target"; break; }
    r = vkAllocateMemory(s.device, &alloc, nullptr, &image_memory);
    if (r != VK_SUCCESS) { why = failure("vkAllocateMemory", r); break; }
    vkBindImageMemory(s.device, image, image_memory, 0);
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = kTargetFormat;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    r = vkCreateImageView(s.device, &view_info, nullptr, &view);
    if (r != VK_SUCCESS) { why = failure("vkCreateImageView", r); break; }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = bytes;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    r = vkCreateBuffer(s.device, &buffer_info, nullptr, &buffer);
    if (r != VK_SUCCESS) { why = failure("vkCreateBuffer", r); break; }
    vkGetBufferMemoryRequirements(s.device, buffer, &need);
    alloc.allocationSize = need.size;
    if (!s.memoryType(need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, alloc.memoryTypeIndex)) {
      why = "no host-visible memory";
      break;
    }
    r = vkAllocateMemory(s.device, &alloc, nullptr, &buffer_memory);
    if (r != VK_SUCCESS) { why = failure("vkAllocateMemory", r); break; }
    vkBindBufferMemory(s.device, buffer, buffer_memory, 0);

    VkCommandBufferAllocateInfo command_info{};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_info.commandPool = s.pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(s.device, &command_info, &commands);
    if (r != VK_SUCCESS) { why = failure("vkAllocateCommandBuffers", r); break; }
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    r = vkCreateFence(s.device, &fence_info, nullptr, &fence);
    if (r != VK_SUCCESS) { why = failure("vkCreateFence", r); break; }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commands, &begin);

    VkImageMemoryBarrier to_target{};
    to_target.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_target.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    to_target.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_target.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_target.srcQueueFamilyIndex = to_target.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_target.image = image;
    to_target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &to_target);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(commands, &rendering);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipeline);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    for (const auto & colour : colours) {
      vkCmdPushConstants(commands, s.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof colour, colour.data());
      vkCmdDraw(commands, 3, 1, 0, 0);
    }
    vkCmdEndRendering(commands);

    VkImageMemoryBarrier to_copy = to_target;
    to_copy.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_copy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_copy.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &to_copy);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyImageToBuffer(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    vkEndCommandBuffer(commands);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands;
    r = vkQueueSubmit(s.queue, 1, &submit, fence);
    if (r != VK_SUCCESS) { why = failure("vkQueueSubmit", r); break; }
    r = vkWaitForFences(s.device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS) { why = failure("vkWaitForFences", r); break; }

    void * mapped = nullptr;
    r = vkMapMemory(s.device, buffer_memory, 0, bytes, 0, &mapped);
    if (r != VK_SUCCESS) { why = failure("vkMapMemory", r); break; }
    const auto * halves = static_cast<const uint16_t *>(mapped);
    rgba.resize(static_cast<size_t>(pixels) * 4);
    for (size_t i = 0; i < rgba.size(); i++) rgba[i] = halfToFloat(halves[i]);
    vkUnmapMemory(s.device, buffer_memory);
    ok = true;
  } while (false);

  if (fence) vkDestroyFence(s.device, fence, nullptr);
  if (commands) vkFreeCommandBuffers(s.device, s.pool, 1, &commands);
  if (buffer) vkDestroyBuffer(s.device, buffer, nullptr);
  if (buffer_memory) vkFreeMemory(s.device, buffer_memory, nullptr);
  if (view) vkDestroyImageView(s.device, view, nullptr);
  if (image) vkDestroyImage(s.device, image, nullptr);
  if (image_memory) vkFreeMemory(s.device, image_memory, nullptr);
  if (!ok) return fail(why);
  return true;
}

}  // namespace cellui::vulkan
