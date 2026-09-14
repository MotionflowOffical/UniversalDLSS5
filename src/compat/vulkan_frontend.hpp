#pragma once
#include "renderer_frontend.hpp"
#include "vulkan_abi.hpp"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace udlss::compat {

struct VulkanDeviceFunctions {
    vkabi::PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr{};
    vkabi::PFN_vkCreateImage vkCreateImage{};
    vkabi::PFN_vkDestroyImage vkDestroyImage{};
    vkabi::PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements{};
    vkabi::PFN_vkAllocateMemory vkAllocateMemory{};
    vkabi::PFN_vkFreeMemory vkFreeMemory{};
    vkabi::PFN_vkBindImageMemory vkBindImageMemory{};
    vkabi::PFN_vkCreateSemaphore vkCreateSemaphore{};
    vkabi::PFN_vkDestroySemaphore vkDestroySemaphore{};
    vkabi::PFN_vkCreateCommandPool vkCreateCommandPool{};
    vkabi::PFN_vkDestroyCommandPool vkDestroyCommandPool{};
    vkabi::PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers{};
    vkabi::PFN_vkResetCommandBuffer vkResetCommandBuffer{};
    vkabi::PFN_vkBeginCommandBuffer vkBeginCommandBuffer{};
    vkabi::PFN_vkEndCommandBuffer vkEndCommandBuffer{};
    vkabi::PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier{};
    vkabi::PFN_vkCmdCopyImage vkCmdCopyImage{};
    vkabi::PFN_vkQueueSubmit vkQueueSubmit{};
    vkabi::PFN_vkQueueWaitIdle vkQueueWaitIdle{};
};

class VulkanFrontend final : public IRendererFrontend {
public:
    VulkanFrontend()=default;
    ~VulkanFrontend() override { reset(); }
    bool initialize(vkabi::VkPhysicalDevice physical,vkabi::VkDevice device,std::uint32_t queueFamily,
                    const vkabi::VkSwapchainCreateInfoKHR& createInfo,
                    std::vector<vkabi::VkImage> swapchainImages,
                    vkabi::PFN_vkGetInstanceProcAddr gipa,vkabi::PFN_vkGetDeviceProcAddr gdpa,
                    RuntimeStatus& status);
    bool preparePresent(vkabi::VkQueue queue,const vkabi::VkPresentInfoKHR& present,std::uint32_t swapchainSlot,RuntimeStatus& status);
    vkabi::VkSemaphore presentWaitSemaphore() const noexcept { return postSemaphore_; }
    bool originalWaitsConsumed() const noexcept { return originalWaitsConsumed_; }
    bool postCopySubmitted() const noexcept { return postCopySubmitted_; }

    RendererRoute route() const noexcept override { return RendererRoute::CompatVulkan; }
    CompatInterop interop() const noexcept override { return CompatInterop::VulkanExternalMemory; }
    std::wstring_view name() const noexcept override { return L"Vulkan external-memory frontend"; }
    ID3D11Device* canonicalDevice() const noexcept override { return d11_.Get(); }
    ID3D11DeviceContext* canonicalContext() const noexcept override { return d11Context_.Get(); }
    bool beginFrame(CompatFrame& frame,RuntimeStatus& status) override;
    bool endFrame(const CompatFrame& frame,RuntimeStatus& status) override;
    void reset() override;

private:
    bool loadFunctions(RuntimeStatus& status);
    bool createD3D11Device(RuntimeStatus& status);
    bool supportsD3D11ExternalMemory(RuntimeStatus& status) const;
    bool createInteropImage(RuntimeStatus& status);
    bool createCommands(RuntimeStatus& status);
    bool submitCopy(bool intoCanonical,RuntimeStatus& status);
    bool waitForD3D11(RuntimeStatus& status);
    std::uint32_t chooseMemoryType(std::uint32_t typeBits) const;
    DXGI_FORMAT dxgiFormat() const noexcept;
    void destroyVulkanObjects();

    vkabi::VkPhysicalDevice physical_{};vkabi::VkDevice device_{};vkabi::VkQueue queue_{};
    std::uint32_t queueFamily_{},imageIndex_{};
    vkabi::VkSwapchainCreateInfoKHR swapDesc_{};std::vector<vkabi::VkImage> swapchainImages_;
    vkabi::PFN_vkGetInstanceProcAddr gipa_{};vkabi::PFN_vkGetDeviceProcAddr gdpa_{};VulkanDeviceFunctions vk_{};
    vkabi::PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2_{};
    vkabi::PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2_{};
    vkabi::PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties_{};
    vkabi::VkPhysicalDeviceMemoryProperties memoryProps_{};
    vkabi::VkImage interopImage_{};vkabi::VkDeviceMemory interopMemory_{};vkabi::VkCommandPool commandPool_{};vkabi::VkCommandBuffer commandBuffer_{};vkabi::VkSemaphore postSemaphore_{};
    Microsoft::WRL::ComPtr<ID3D11Device> d11_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> d11Context_;Microsoft::WRL::ComPtr<ID3D11Texture2D> canonical_;Microsoft::WRL::ComPtr<ID3D11Query> completionQuery_;
    vkabi::VkPresentInfoKHR pendingPresent_{};std::vector<vkabi::VkSemaphore> pendingWaits_;std::vector<vkabi::VkPipelineStageFlags> pendingWaitStages_;
    std::uint64_t generation_{};bool initialized_{},originalWaitsConsumed_{},postCopySubmitted_{};
};

} // namespace udlss::compat
