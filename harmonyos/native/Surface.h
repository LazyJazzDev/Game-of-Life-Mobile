#pragma once
#include <native_window/external_window.h>
#include <vulkan/vulkan.h>

#include "grassland/rhi/backend/vulkan/vulkan_device.h"
#include "grassland/rhi/backend/vulkan/vulkan_image.h"

namespace longmarch::harmony {
class Surface {
 public:
  static std::unique_ptr<Surface> Create(grassland::rhi::IDevice *device, OHNativeWindow *window, std::string &error);
  ~Surface();
  bool Resize(uint32_t width, uint32_t height);
  // Blits the game's display-encoded image, scaled to fit, onto an SDR swapchain.
  bool Present(grassland::rhi::IImage *image);

  const std::string &Error() const {
    return error_;
  }

 private:
  Surface(grassland::rhi::IDevice *device, OHNativeWindow *window);
  bool Initialize();
  bool Check(VkResult result, const char *operation);
  bool Fail(const std::string &message);
  std::string error_;
  void ReleaseSwapchain();

  // Presentation work in flight. The blit waits for the acquired image and
  // signals presentation on the GPU, so the render thread never blocks on it.
  struct Frame {
    grassland::rhi::ObjPtr<grassland::rhi::IImage> source;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence done = VK_NULL_HANDLE;
  };

  static constexpr size_t kFramesInFlight = 2;

  grassland::rhi::ObjPtr<grassland::rhi::backend::vulkan::IVulkanDevice> core_;
  OHNativeWindow *window_;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  Frame frames_[kFramesInFlight];
  size_t frame_ = 0;
  std::vector<VkImage> images_;
  // Reuse only after reacquiring the same image (presentation consumed its wait).
  std::vector<VkSemaphore> presented_;
  bool poisoned_ = false;
  VkExtent2D extent_{};
  uint32_t requested_width_ = 0, requested_height_ = 0;
};
}  // namespace longmarch::harmony
