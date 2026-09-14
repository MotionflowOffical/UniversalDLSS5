#pragma once
// Minimal Vulkan ABI used by UniversalDLSS5's dynamic Vulkan frontend.
// Deliberately avoids a build-time Vulkan SDK/link dependency.
#include <windows.h>
#include <cstddef>
#include <cstdint>

#ifndef VKAPI_ATTR
#define VKAPI_ATTR
#endif
#ifndef VKAPI_CALL
#define VKAPI_CALL __stdcall
#endif
#ifndef VKAPI_PTR
#define VKAPI_PTR VKAPI_CALL
#endif

namespace udlss::compat::vkabi {
using VkFlags=std::uint32_t; using VkBool32=std::uint32_t; using VkDeviceSize=std::uint64_t;
using VkResult=std::int32_t; using VkStructureType=std::int32_t; using VkFormat=std::int32_t;
using VkImageLayout=std::int32_t; using VkImageType=std::int32_t; using VkImageTiling=std::int32_t;
using VkSharingMode=std::int32_t; using VkCommandBufferLevel=std::int32_t; using VkPipelineStageFlags=VkFlags;
using VkAccessFlags=VkFlags; using VkImageUsageFlags=VkFlags; using VkImageCreateFlags=VkFlags;
using VkMemoryPropertyFlags=VkFlags; using VkExternalMemoryHandleTypeFlags=VkFlags; using VkExternalMemoryFeatureFlags=VkFlags;
using VkCommandPoolCreateFlags=VkFlags; using VkCommandBufferUsageFlags=VkFlags; using VkImageAspectFlags=VkFlags;
using VkSampleCountFlagBits=VkFlags;

struct VkInstance_T; struct VkPhysicalDevice_T; struct VkDevice_T; struct VkQueue_T; struct VkCommandBuffer_T;
using VkInstance=VkInstance_T*; using VkPhysicalDevice=VkPhysicalDevice_T*; using VkDevice=VkDevice_T*; using VkQueue=VkQueue_T*; using VkCommandBuffer=VkCommandBuffer_T*;
using VkSurfaceKHR=std::uint64_t; using VkSwapchainKHR=std::uint64_t; using VkImage=std::uint64_t; using VkDeviceMemory=std::uint64_t; using VkSemaphore=std::uint64_t; using VkCommandPool=std::uint64_t; using VkFence=std::uint64_t;
using PFN_vkVoidFunction=void(VKAPI_PTR*)();

inline constexpr VkResult VK_SUCCESS=0;
inline constexpr std::uint64_t VK_NULL_HANDLE=0;
inline constexpr std::uint32_t VK_QUEUE_FAMILY_IGNORED=0xffffffffu;

inline constexpr VkStructureType VK_STRUCTURE_TYPE_SUBMIT_INFO=4;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO=5;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO=9;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO=14;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO=39;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO=40;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO=42;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER=45;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR=1000001000;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_PRESENT_INFO_KHR=1000001001;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2=1000059004;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2=1000059003;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO=1000071000;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES=1000071001;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO=1000072000;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR=1000073000;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2=1000059001;
inline constexpr VkStructureType VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES=1000071004;

inline constexpr VkExternalMemoryHandleTypeFlags VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT=0x00000008u;
inline constexpr VkExternalMemoryFeatureFlags VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT=0x00000004u;
inline constexpr VkImageUsageFlags VK_IMAGE_USAGE_TRANSFER_SRC_BIT=0x00000001u;
inline constexpr VkImageUsageFlags VK_IMAGE_USAGE_TRANSFER_DST_BIT=0x00000002u;
inline constexpr VkSampleCountFlagBits VK_SAMPLE_COUNT_1_BIT=0x00000001u;
inline constexpr VkCommandPoolCreateFlags VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT=0x00000002u;
inline constexpr VkCommandBufferUsageFlags VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT=0x00000001u;
inline constexpr VkPipelineStageFlags VK_PIPELINE_STAGE_TRANSFER_BIT=0x00001000u;
inline constexpr VkAccessFlags VK_ACCESS_TRANSFER_READ_BIT=0x00000800u;
inline constexpr VkAccessFlags VK_ACCESS_TRANSFER_WRITE_BIT=0x00001000u;
inline constexpr VkMemoryPropertyFlags VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT=0x00000001u;
inline constexpr VkImageAspectFlags VK_IMAGE_ASPECT_COLOR_BIT=0x00000001u;
inline constexpr VkImageType VK_IMAGE_TYPE_2D=1;
inline constexpr VkImageTiling VK_IMAGE_TILING_OPTIMAL=0;
inline constexpr VkSharingMode VK_SHARING_MODE_EXCLUSIVE=0;
inline constexpr VkCommandBufferLevel VK_COMMAND_BUFFER_LEVEL_PRIMARY=0;
inline constexpr VkImageLayout VK_IMAGE_LAYOUT_UNDEFINED=0;
inline constexpr VkImageLayout VK_IMAGE_LAYOUT_GENERAL=1;
inline constexpr VkImageLayout VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL=6;
inline constexpr VkImageLayout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL=7;
inline constexpr VkImageLayout VK_IMAGE_LAYOUT_PRESENT_SRC_KHR=1000001002;

inline constexpr VkFormat VK_FORMAT_R8G8B8A8_UNORM=37;
inline constexpr VkFormat VK_FORMAT_R8G8B8A8_SRGB=43;
inline constexpr VkFormat VK_FORMAT_B8G8R8A8_UNORM=44;
inline constexpr VkFormat VK_FORMAT_B8G8R8A8_SRGB=50;
inline constexpr VkFormat VK_FORMAT_A2B10G10R10_UNORM_PACK32=64;
inline constexpr VkFormat VK_FORMAT_R16G16B16A16_SFLOAT=97;

struct VkExtent2D{std::uint32_t width,height;};
struct VkExtent3D{std::uint32_t width,height,depth;};
struct VkOffset3D{std::int32_t x,y,z;};
struct VkImageSubresourceLayers{VkImageAspectFlags aspectMask;std::uint32_t mipLevel,baseArrayLayer,layerCount;};
struct VkImageCopy{VkImageSubresourceLayers srcSubresource;VkOffset3D srcOffset;VkImageSubresourceLayers dstSubresource;VkOffset3D dstOffset;VkExtent3D extent;};
struct VkImageFormatProperties{VkExtent3D maxExtent;std::uint32_t maxMipLevels,maxArrayLayers;VkFlags sampleCounts;VkDeviceSize maxResourceSize;};
struct VkExternalMemoryProperties{VkExternalMemoryFeatureFlags externalMemoryFeatures;VkExternalMemoryHandleTypeFlags exportFromImportedHandleTypes;VkExternalMemoryHandleTypeFlags compatibleHandleTypes;};

struct VkPhysicalDeviceExternalImageFormatInfo{VkStructureType sType;const void* pNext;VkExternalMemoryHandleTypeFlags handleType;};
struct VkPhysicalDeviceImageFormatInfo2{VkStructureType sType;const void* pNext;VkFormat format;VkImageType type;VkImageTiling tiling;VkImageUsageFlags usage;VkImageCreateFlags flags;};
struct VkExternalImageFormatProperties{VkStructureType sType;void* pNext;VkExternalMemoryProperties externalMemoryProperties;};
struct VkImageFormatProperties2{VkStructureType sType;void* pNext;VkImageFormatProperties imageFormatProperties;};
struct VkExternalMemoryImageCreateInfo{VkStructureType sType;const void* pNext;VkExternalMemoryHandleTypeFlags handleTypes;};
struct VkImportMemoryWin32HandleInfoKHR{VkStructureType sType;const void* pNext;VkExternalMemoryHandleTypeFlags handleType;HANDLE handle;LPCWSTR name;};
struct VkPhysicalDeviceIDProperties{VkStructureType sType;void* pNext;std::uint8_t deviceUUID[16];std::uint8_t driverUUID[16];std::uint8_t deviceLUID[8];std::uint32_t deviceNodeMask;VkBool32 deviceLUIDValid;};
struct VkPhysicalDeviceProperties2Raw{VkStructureType sType;void* pNext;alignas(8) std::byte properties[2048];};

struct VkImageCreateInfo{VkStructureType sType;const void* pNext;VkImageCreateFlags flags;VkImageType imageType;VkFormat format;VkExtent3D extent;std::uint32_t mipLevels,arrayLayers;VkSampleCountFlagBits samples;VkImageTiling tiling;VkImageUsageFlags usage;VkSharingMode sharingMode;std::uint32_t queueFamilyIndexCount;const std::uint32_t* pQueueFamilyIndices;VkImageLayout initialLayout;};
struct VkMemoryRequirements{VkDeviceSize size,alignment;std::uint32_t memoryTypeBits;};
struct VkMemoryAllocateInfo{VkStructureType sType;const void* pNext;VkDeviceSize allocationSize;std::uint32_t memoryTypeIndex;};
struct VkMemoryType{VkMemoryPropertyFlags propertyFlags;std::uint32_t heapIndex;};
struct VkMemoryHeap{VkDeviceSize size;VkFlags flags;};
struct VkPhysicalDeviceMemoryProperties{std::uint32_t memoryTypeCount;VkMemoryType memoryTypes[32];std::uint32_t memoryHeapCount;VkMemoryHeap memoryHeaps[16];};
struct VkSemaphoreCreateInfo{VkStructureType sType;const void* pNext;VkFlags flags;};
struct VkCommandPoolCreateInfo{VkStructureType sType;const void* pNext;VkCommandPoolCreateFlags flags;std::uint32_t queueFamilyIndex;};
struct VkCommandBufferAllocateInfo{VkStructureType sType;const void* pNext;VkCommandPool commandPool;VkCommandBufferLevel level;std::uint32_t commandBufferCount;};
struct VkCommandBufferBeginInfo{VkStructureType sType;const void* pNext;VkCommandBufferUsageFlags flags;const void* pInheritanceInfo;};
struct VkImageMemoryBarrier{VkStructureType sType;const void* pNext;VkAccessFlags srcAccessMask,dstAccessMask;VkImageLayout oldLayout,newLayout;std::uint32_t srcQueueFamilyIndex,dstQueueFamilyIndex;VkImage image;struct{VkImageAspectFlags aspectMask;std::uint32_t baseMipLevel,levelCount,baseArrayLayer,layerCount;} subresourceRange;};
struct VkSubmitInfo{VkStructureType sType;const void* pNext;std::uint32_t waitSemaphoreCount;const VkSemaphore* pWaitSemaphores;const VkPipelineStageFlags* pWaitDstStageMask;std::uint32_t commandBufferCount;const VkCommandBuffer* pCommandBuffers;std::uint32_t signalSemaphoreCount;const VkSemaphore* pSignalSemaphores;};
struct VkSwapchainCreateInfoKHR{VkStructureType sType;const void* pNext;VkFlags flags;VkSurfaceKHR surface;std::uint32_t minImageCount;VkFormat imageFormat;std::int32_t imageColorSpace;VkExtent2D imageExtent;std::uint32_t imageArrayLayers;VkImageUsageFlags imageUsage;VkSharingMode imageSharingMode;std::uint32_t queueFamilyIndexCount;const std::uint32_t* pQueueFamilyIndices;VkFlags preTransform,compositeAlpha;std::int32_t presentMode;VkBool32 clipped;VkSwapchainKHR oldSwapchain;};
struct VkPresentInfoKHR{VkStructureType sType;const void* pNext;std::uint32_t waitSemaphoreCount;const VkSemaphore* pWaitSemaphores;std::uint32_t swapchainCount;const VkSwapchainKHR* pSwapchains;const std::uint32_t* pImageIndices;VkResult* pResults;};

using PFN_vkGetInstanceProcAddr=PFN_vkVoidFunction(VKAPI_PTR*)(VkInstance,const char*);
using PFN_vkGetDeviceProcAddr=PFN_vkVoidFunction(VKAPI_PTR*)(VkDevice,const char*);
using PFN_vkCreateDevice=VkResult(VKAPI_PTR*)(VkPhysicalDevice,const void*,const void*,VkDevice*);
using PFN_vkDestroyDevice=void(VKAPI_PTR*)(VkDevice,const void*);
using PFN_vkGetDeviceQueue=void(VKAPI_PTR*)(VkDevice,std::uint32_t,std::uint32_t,VkQueue*);
using PFN_vkCreateSwapchainKHR=VkResult(VKAPI_PTR*)(VkDevice,const VkSwapchainCreateInfoKHR*,const void*,VkSwapchainKHR*);
using PFN_vkDestroySwapchainKHR=void(VKAPI_PTR*)(VkDevice,VkSwapchainKHR,const void*);
using PFN_vkGetSwapchainImagesKHR=VkResult(VKAPI_PTR*)(VkDevice,VkSwapchainKHR,std::uint32_t*,VkImage*);
using PFN_vkQueuePresentKHR=VkResult(VKAPI_PTR*)(VkQueue,const VkPresentInfoKHR*);
using PFN_vkGetPhysicalDeviceImageFormatProperties2=VkResult(VKAPI_PTR*)(VkPhysicalDevice,const VkPhysicalDeviceImageFormatInfo2*,VkImageFormatProperties2*);
using PFN_vkGetPhysicalDeviceProperties2=void(VKAPI_PTR*)(VkPhysicalDevice,VkPhysicalDeviceProperties2Raw*);
using PFN_vkGetPhysicalDeviceMemoryProperties=void(VKAPI_PTR*)(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties*);
using PFN_vkCreateImage=VkResult(VKAPI_PTR*)(VkDevice,const VkImageCreateInfo*,const void*,VkImage*);
using PFN_vkDestroyImage=void(VKAPI_PTR*)(VkDevice,VkImage,const void*);
using PFN_vkGetImageMemoryRequirements=void(VKAPI_PTR*)(VkDevice,VkImage,VkMemoryRequirements*);
using PFN_vkAllocateMemory=VkResult(VKAPI_PTR*)(VkDevice,const VkMemoryAllocateInfo*,const void*,VkDeviceMemory*);
using PFN_vkFreeMemory=void(VKAPI_PTR*)(VkDevice,VkDeviceMemory,const void*);
using PFN_vkBindImageMemory=VkResult(VKAPI_PTR*)(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize);
using PFN_vkCreateSemaphore=VkResult(VKAPI_PTR*)(VkDevice,const VkSemaphoreCreateInfo*,const void*,VkSemaphore*);
using PFN_vkDestroySemaphore=void(VKAPI_PTR*)(VkDevice,VkSemaphore,const void*);
using PFN_vkCreateCommandPool=VkResult(VKAPI_PTR*)(VkDevice,const VkCommandPoolCreateInfo*,const void*,VkCommandPool*);
using PFN_vkDestroyCommandPool=void(VKAPI_PTR*)(VkDevice,VkCommandPool,const void*);
using PFN_vkAllocateCommandBuffers=VkResult(VKAPI_PTR*)(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer*);
using PFN_vkResetCommandBuffer=VkResult(VKAPI_PTR*)(VkCommandBuffer,VkFlags);
using PFN_vkBeginCommandBuffer=VkResult(VKAPI_PTR*)(VkCommandBuffer,const VkCommandBufferBeginInfo*);
using PFN_vkEndCommandBuffer=VkResult(VKAPI_PTR*)(VkCommandBuffer);
using PFN_vkCmdPipelineBarrier=void(VKAPI_PTR*)(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkFlags,std::uint32_t,const void*,std::uint32_t,const void*,std::uint32_t,const VkImageMemoryBarrier*);
using PFN_vkCmdCopyImage=void(VKAPI_PTR*)(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,std::uint32_t,const VkImageCopy*);
using PFN_vkQueueSubmit=VkResult(VKAPI_PTR*)(VkQueue,std::uint32_t,const VkSubmitInfo*,VkFence);
using PFN_vkQueueWaitIdle=VkResult(VKAPI_PTR*)(VkQueue);

} // namespace udlss::compat::vkabi
