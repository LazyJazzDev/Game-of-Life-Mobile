#include "Surface.h"

#include <hilog/log.h>

#include <cmath>
#include <limits>

#include "grassland/rhi/backend/vulkan/vulkan_queue.h"

using namespace grassland;
namespace vulkan = grassland::rhi::backend::vulkan;

namespace longmarch::harmony {
std::unique_ptr<Surface> Surface::Create(rhi::IDevice *device, OHNativeWindow *window, std::string &error) {
  auto result = std::unique_ptr<Surface>(new Surface(device, window));
  if (!result->Initialize()) {
    error = result->Error();
    return {};
  }
  return result;
}

Surface::Surface(rhi::IDevice *device, OHNativeWindow *window)
    : core_(rhi::QueryInterfacePtr<vulkan::IVulkanDevice>(device)),
      window_(window) {
}

bool Surface::Fail(const std::string &message) {
  error_ = message;
  OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "LongMarchGPU", "%{public}s", message.c_str());
  return false;
}

bool Surface::Check(VkResult result, const char *operation) {
  return result == VK_SUCCESS || Fail(std::string(operation) + ": " + std::to_string(result));
}

bool Surface::Initialize() {
  if (!core_ || !window_)
    return Fail("Missing Vulkan device or native window");
  auto create =
      reinterpret_cast<PFN_vkCreateSurfaceOHOS>(vkGetInstanceProcAddr(core_->Instance(), "vkCreateSurfaceOHOS"));
  if (!create)
    return Fail("VK_OHOS_surface is unavailable");
  const auto physical_device = core_->NativePhysicalDevice();
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_device, &properties);
  OH_LOG_Print(LOG_APP, LOG_INFO, 0, "LongMarchGPU", "GPU=%{public}s API=%{public}u.%{public}u", properties.deviceName,
               VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion));
  VkSurfaceCreateInfoOHOS info{};
  info.sType = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
  info.window = window_;
  if (!Check(create(core_->Instance(), &info, nullptr, &surface_), "Create OHOS surface"))
    return false;
  VkBool32 supported = VK_FALSE;
  VkResult result =
      vkGetPhysicalDeviceSurfaceSupportKHR(core_->NativePhysicalDevice(), core_->QueueFamily(), surface_, &supported);
  if (result != VK_SUCCESS || !supported) {
    vkDestroySurfaceKHR(core_->Instance(), surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    return Fail("Graphics queue cannot present to this display");
  }
  auto device = core_->NativeDevice();
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool.queueFamilyIndex = core_->QueueFamily();
  if (!Check(vkCreateCommandPool(device, &pool, nullptr, &command_pool_), "Presentation command pool"))
    return false;
  for (auto &frame : frames_) {
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = command_pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    if (!Check(vkAllocateCommandBuffers(device, &allocate, &frame.commands), "Presentation commands"))
      return false;
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!Check(vkCreateSemaphore(device, &semaphore, nullptr, &frame.acquired), "Acquire semaphore"))
      return false;
    if (!Check(vkCreateSemaphore(device, &semaphore, nullptr, &frame.blitted), "Blit semaphore"))
      return false;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (!Check(vkCreateFence(device, &fence, nullptr, &frame.done), "Presentation fence"))
      return false;
  }
  return true;
}

Surface::~Surface() {
  if (!core_)
    return;
  vkDeviceWaitIdle(core_->NativeDevice());
  ReleaseSwapchain();
  for (auto &frame : frames_) {
    if (frame.done)
      vkDestroyFence(core_->NativeDevice(), frame.done, nullptr);
    if (frame.acquired)
      vkDestroySemaphore(core_->NativeDevice(), frame.acquired, nullptr);
    if (frame.blitted)
      vkDestroySemaphore(core_->NativeDevice(), frame.blitted, nullptr);
  }
  if (command_pool_)
    vkDestroyCommandPool(core_->NativeDevice(), command_pool_, nullptr);
  if (surface_)
    vkDestroySurfaceKHR(core_->Instance(), surface_, nullptr);
}

void Surface::ReleaseSwapchain() {
  if (swapchain_)
    vkDestroySwapchainKHR(core_->NativeDevice(), swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
  images_.clear();
}

bool Surface::Resize(uint32_t width, uint32_t height) {
  error_.clear();
  if (!width || !height)
    return true;
  if (swapchain_ && width == requested_width_ && height == requested_height_)
    return true;
  if (!Check(vkDeviceWaitIdle(core_->NativeDevice()), "Wait before resize"))
    return false;
  ReleaseSwapchain();
  requested_width_ = width;
  requested_height_ = height;
  auto device = core_->NativeDevice();
  auto physical = core_->NativePhysicalDevice();
  VkSurfaceCapabilitiesKHR caps{};
  if (!Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface_, &caps), "Surface capabilities"))
    return false;
  if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    return Fail("Display does not support Vulkan transfer presentation");
  uint32_t count = 0;
  if (!Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface_, &count, nullptr), "Surface formats"))
    return false;
  std::vector<VkSurfaceFormatKHR> formats(count);
  if (!Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface_, &count, formats.data()), "Surface formats"))
    return false;
  if (formats.empty())
    return Fail("Display has no Vulkan surface formats");
  auto format = formats.front();
  for (auto candidate : formats) {
    if ((candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_R8G8B8A8_UNORM) &&
        candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      format = candidate;
  }
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(physical, format.format, &properties);
  if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
    return Fail("Display format does not support image blits");
  extent_ = caps.currentExtent;
  if (extent_.width == UINT32_MAX) {
    extent_.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent_.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }
  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = surface_;
  info.minImageCount = std::max(2u, caps.minImageCount);
  if (caps.maxImageCount)
    info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
  info.imageFormat = format.format;
  info.imageColorSpace = format.colorSpace;
  info.imageExtent = extent_;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  // Render in ArkUI's current orientation; let the compositor transform it.
  info.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                          : caps.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                     VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
    if (caps.supportedCompositeAlpha & alpha) {
      info.compositeAlpha = alpha;
      break;
    }
  }
  info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  info.clipped = VK_TRUE;
  if (!Check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain_), "Create swapchain"))
    return false;
  // Game and UI colors are display-encoded sRGB.
  const int color_result = OH_NativeWindow_SetColorSpace(window_, OH_COLORSPACE_SRGB_FULL);
  if (color_result)
    OH_LOG_Print(LOG_APP, LOG_WARN, 0, "LongMarchGPU", "sRGB color space result=%{public}d", color_result);
  if (!Check(vkGetSwapchainImagesKHR(device, swapchain_, &count, nullptr), "Swapchain images"))
    return false;
  images_.resize(count);
  if (!Check(vkGetSwapchainImagesKHR(device, swapchain_, &count, images_.data()), "Swapchain images"))
    return false;
  return true;
}

bool Surface::Present(rhi::IImage *source) {
  error_.clear();
  if (!swapchain_ || !source)
    return false;
  auto image = rhi::QueryInterfacePtr<vulkan::IVulkanImage>(source);
  if (!image)
    return Fail("Presentation requires a Vulkan image");
  std::lock_guard<std::mutex> queue_lock(core_->GetNativeQueue(rhi::QUEUE_TYPE_MAIN)->QueueMutex());
  auto device = core_->NativeDevice();
  auto &frame = frames_[frame_];
  // Reuse this slot only after its previous blit, which bounds work in flight.
  if (!Check(vkWaitForFences(device, 1, &frame.done, VK_TRUE, UINT64_MAX), "Wait presentation"))
    return false;
  frame.source = rhi::ObjPtr<rhi::IImage>::Retain(source);
  uint32_t index = 0;
  VkResult acquire = vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, frame.acquired, VK_NULL_HANDLE, &index);
  if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
    vkDeviceWaitIdle(core_->NativeDevice());
    ReleaseSwapchain();
    Resize(requested_width_, requested_height_);
    return false;
  }
  if (acquire != VK_SUBOPTIMAL_KHR)
    if (!Check(acquire, "Acquire display image"))
      return false;
  if (!Check(vkResetFences(device, 1, &frame.done), "Reset presentation fence"))
    return false;
  frame_ = (frame_ + 1) % kFramesInFlight;
  auto size = source->GetDesc();
  const double scale = std::min(double(extent_.width) / size.width, double(extent_.height) / size.height);
  const double left = (extent_.width - size.width * scale) / 2;
  const double top = (extent_.height - size.height * scale) / 2;
  VkImageBlit blit{};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstSubresource = blit.srcSubresource;
  auto clip = [](double value, uint32_t bound) { return int32_t(std::clamp(value, 0.0, double(bound))); };
  blit.dstOffsets[0] = {clip(left, extent_.width), clip(top, extent_.height), 0};
  blit.dstOffsets[1] = {clip(left + size.width * scale, extent_.width), clip(top + size.height * scale, extent_.height),
                        1};
  blit.srcOffsets[0] = {clip((blit.dstOffsets[0].x - left) / scale, size.width),
                        clip((blit.dstOffsets[0].y - top) / scale, size.height), 0};
  blit.srcOffsets[1] = {clip((blit.dstOffsets[1].x - left) / scale, size.width),
                        clip((blit.dstOffsets[1].y - top) / scale, size.height), 1};
  {
    VkCommandBuffer cmd = frame.commands;
    if (!Check(vkResetCommandBuffer(cmd, 0), "Reset presentation commands"))
      return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!Check(vkBeginCommandBuffer(cmd, &begin), "Begin presentation commands"))
      return false;
    auto transit = [&](VkImage target, VkImageLayout before, VkImageLayout after, VkAccessFlags src,
                       VkAccessFlags dst) {
      VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      barrier.srcAccessMask = src;
      barrier.dstAccessMask = dst;
      barrier.oldLayout = before;
      barrier.newLayout = after;
      barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = target;
      barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                           0, nullptr, 1, &barrier);
    };
    transit(images_[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearColorValue black{};
    black.float32[3] = 1;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    transit(images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    transit(image->NativeImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    if (blit.dstOffsets[1].x > blit.dstOffsets[0].x && blit.dstOffsets[1].y > blit.dstOffsets[0].y &&
        blit.srcOffsets[1].x > blit.srcOffsets[0].x && blit.srcOffsets[1].y > blit.srcOffsets[0].y) {
      vkCmdBlitImage(cmd, image->NativeImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, images_[index],
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    }
    transit(image->NativeImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    transit(images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    if (!Check(vkEndCommandBuffer(cmd), "End presentation commands"))
      return false;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &frame.blitted;
    if (!Check(vkQueueSubmit(core_->GetNativeQueue(rhi::QUEUE_TYPE_MAIN)->NativeQueue(), 1, &submit, frame.done),
               "Submit presentation"))
      return false;
  }
  VkPresentInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &frame.blitted;
  info.swapchainCount = 1;
  info.pSwapchains = &swapchain_;
  info.pImageIndices = &index;
  VkResult result = vkQueuePresentKHR(core_->GetNativeQueue(rhi::QUEUE_TYPE_MAIN)->NativeQueue(), &info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    vkDeviceWaitIdle(core_->NativeDevice());
    ReleaseSwapchain();
    return false;
  }
  if (!Check(result, "Present display image"))
    return false;
  return true;
}
}  // namespace longmarch::harmony
