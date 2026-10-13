#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <optional>
#include <string>
#include <cstddef>

#include <glm/glm.hpp>

#include "../Renderer.h"
#include "scene/Vertex.h"
#include "scene/MeshLibrary.h"
#include "scene/MaterialLibrary.h"
#include "platform/FileWatch.h"
#include "scene/Environment.h"

#include <vk_mem_alloc.h>

enum class MemoryUse
{
	// 先通过暂存缓冲区由 CPU 写入一次，然后由 GPU 读取。
	DeviceLocal,

	// CPU 写入，GPU 读取：暂存缓冲区和每帧 uniform buffer。
	// 创建时已映射，所以指针来自 VmaAllocationInfo::pMappedData，而不是单独的 vmaMapMemory 调用。
	HostVisible,
};

struct PushConstants
{
	glm::mat4 model;
	glm::vec4 tint;
	glm::vec4 material;
	glm::vec3 emissive;
    alignas(16) glm::vec4 normalInfo;
};

static_assert(offsetof(PushConstants, emissive) == 96,
    "emissive must match the shader push constant offset");
static_assert(offsetof(PushConstants, normalInfo) == 112, "normalInfo must match shader offset");
static_assert(sizeof(PushConstants) <= 128,
    "mesh push constants must fit Vulkan's minimum supported size");

struct UniformBufferObject {
	glm::mat4 view;
	glm::mat4 proj;
	glm::vec4 cameraPosition;
	glm::vec4 lightDirection;
	glm::vec4 lightColor;
	glm::mat4 lightViewProjection;
	glm::mat4 inverseViewProjection;
	glm::vec4 environment;
	glm::vec4 lightingTerms;
	glm::vec4 framebufferSize;
};
static_assert(offsetof(UniformBufferObject, environment) == 304 && sizeof(UniformBufferObject) == 352,
    "environment camera UBO must match std140 shader offsets");

struct QueueFamilyIndices {
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;

	bool isComplete() {
		return graphicsFamily.has_value() && presentFamily.has_value();
	}
};

struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

class VulkanRenderer : public Renderer{
public:
	static constexpr uint32_t kMaxFramesInFlight = 2;

	VulkanRenderer() = default;
	~VulkanRenderer() override = default;

	VulkanRenderer(const VulkanRenderer&) = delete;
	VulkanRenderer& operator=(const VulkanRenderer&) = delete;

	virtual void ConfigureWindowHints() const override;
	virtual void Init(GLFWwindow* window) override;
	virtual void Cleanup() override;
	virtual void WaitIdle() override;
	virtual void DrawFrame(const RenderSnapshot& snapshot,
		const std::function<void()>& drawEditorUi) override;
	
private:

	// one-time initialisation
	void CreateInstance();
	void SetupDebugMessenger();
	void CreateSurface();
	void PickPhysicalDevice();
	void CreateLogicalDevice();
	void CreateCommandPool();
	void CreateCommandBuffers();
	void CreateSyncObjects();
	void CreateRenderFinishedSemaphores();
	void DestroyRenderFinishedSemaphores();

	// resources sized to the swapchain
	void CreateSwapChain();
	void CreateImageViews();
	void CreateDepthResources();
	void CreateHdrResources();
	void DestroyHdrResources();
	void CreateRenderPass();
	void CreateGraphicsPipeline();
	void CreateToneMapPipeline();
	void CreateShadowResources();
	void DestroyShadowResources();
	void CreateShadowRenderPass();
	void CreateShadowPipeline();
	void RecordShadowPass(VkCommandBuffer commandBuffer, const RenderSnapshot& snapshot);
	void CreateFramebuffers();

	void DestroySwapChainResources();
	void DestroyPipelineResources();
	void CleanupSwapChain();
	void RecreateSwapChain();
	// ----------------------------------------

	void CreateDescriptorSetLayout();
	void CreateToneMapDescriptorSet();
	void CreateUniformBuffers();
	void CreateDescriptorPool();
	void CreateDescriptorSets();
	void WriteTextureDescriptors();

	void CreateTextureImage();
	void GenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image,
		int32_t width, int32_t height, uint32_t mipLevels) const;
	void UploadTexture(const Image& image);
	void EnsureMaterialTexturesUploaded(const MaterialLibrary& materials);
	void DestroyMaterialTextures();
	void DestroyTextureResources();
	void ReloadTexture();
	void CreateTextureSampler(uint32_t mipLevels);

	// per frame
	// 幂等的"确保已上传"：每帧调用，自己判断有没有新条目。
	void EnsureMeshesUploaded(const MeshLibrary& meshes);
	void UploadMesh(const MeshData& mesh, uint32_t meshIndex);
	void UpdateUniformBuffer(uint32_t frameIndex, const RenderSnapshot& snapshot);
	void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
		const RenderSnapshot& snapshot);

	// debug overlay
	void InitImGui();
	void InitImGuiVulkanBackend();
	void ShutdownImGui();
	void BuildImGuiFrame(const std::function<void()>& drawEditorUi);

	// helper
	std::vector<const char*> GetRequiredExtensions();
	bool CheckValidationLayerSupport();
	VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
	void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
	void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
	bool IsDeviceSuitable(VkPhysicalDevice device);
	QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
	bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
	int RateDeviceSuitability(VkPhysicalDevice device);
	VkFormat FindDepthFormat() const;
	VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates,
		VkImageTiling tiling,
		VkFormatFeatureFlags features) const;
	VkShaderModule CreateShaderModule(const std::vector<char>& code) const;

	void CreateAllocator();

	void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, MemoryUse memoryUse, VkBuffer& buffer, VmaAllocation& allocation, VmaAllocationInfo* allocationInfo = nullptr) const;
	void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

	void UploadToDeviceLocal(const void* source, VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkBuffer& buffer, VmaAllocation& allocation);
	void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkImage& image, VmaAllocation& allocation, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0);
	VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
		uint32_t mipLevels, bool cube = false) const;
	void TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t baseMipLevel, uint32_t levelCount, uint32_t layerCount = 1) const;
	void CopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
	
	void DestroyBuffer(VkBuffer& buffer, VmaAllocation& allocation);
	void DestroyImage(VkImage& image, VkImageView& imageView, VmaAllocation& allocation, VkSampler& imageSampler);

	VkCommandBuffer BeginSingleTimeCommands();
	void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

	SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
	VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
	VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
	VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

	// member
	GLFWwindow* m_window;
	VkInstance m_instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	VkQueue m_presentQueue = VK_NULL_HANDLE;
	std::string m_deviceName;

	// resources sized to the swapchain
	VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
	std::vector<VkImage> m_swapChainImages;
	VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D m_swapChainExtent{};
	uint32_t m_minImageCount = 0;
	std::vector<VkImageView> m_swapChainImageViews;
	std::vector<VkFramebuffer> m_swapChainFramebuffers;

	VmaAllocator m_allocator = nullptr;

	VkImage m_depthImage = VK_NULL_HANDLE;
	VmaAllocation m_depthImageAllocation = nullptr;
	VkImageView m_depthImageView = VK_NULL_HANDLE;
	VkImage m_hdrImage = VK_NULL_HANDLE;
	VmaAllocation m_hdrImageAllocation = nullptr;
	VkImageView m_hdrImageView = VK_NULL_HANDLE;
	VkFormat m_hdrFormat = VK_FORMAT_UNDEFINED;

	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline m_mirroredPipeline = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_toneMapDescriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorSet m_toneMapDescriptorSet = VK_NULL_HANDLE;
	VkPipelineLayout m_toneMapPipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_toneMapPipeline = VK_NULL_HANDLE;
	std::array<VkImage, kMaxFramesInFlight> m_shadowImages{};
	std::array<VmaAllocation, kMaxFramesInFlight> m_shadowImageAllocations{};
	std::array<VkImageView, kMaxFramesInFlight> m_shadowImageViews{};
	VkSampler m_shadowSampler = VK_NULL_HANDLE;
	std::array<VkFramebuffer, kMaxFramesInFlight> m_shadowFramebuffers{};
	VkFormat m_shadowDepthFormat = VK_FORMAT_UNDEFINED;
	VkRenderPass m_shadowRenderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_shadowPipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_shadowPipeline = VK_NULL_HANDLE;

	// 场景资源。刻意与 swapchain 大小的那一组分开：这些都不依赖于尺寸，调整大小时不会影响它们。
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> m_descriptorSets;

	std::vector<VkBuffer> m_uniformBuffers;
	std::vector<VmaAllocation> m_uniformBufferAllocations;
	std::vector<void*> m_uniformBuffersMapped;

	FileWatch m_textureWatch;

	struct GpuMesh
	{
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = nullptr;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexAllocation = nullptr;
        uint32_t indexCount = 0;
	};
	std::vector<GpuMesh> m_meshes;
	uint64_t m_meshLibraryIdentity = 0;
	uint64_t m_meshLibraryRevision = UINT64_MAX;
	struct GpuTexture {
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
	};
    // 复用 staging/mip 路径并恢复默认 checkerboard 的成员；只在资源替换时调用。
    GpuTexture UploadMaterialTexture(const Image& image);
    GpuTexture m_flatNormalTexture;
    GpuTexture UploadFloatTexture(const FloatTexture& texture);
    void CreateEnvironmentDescriptorSets();
    void EnsureEnvironmentUploaded(std::shared_ptr<const EnvironmentData> data);
    void WriteEnvironmentDescriptors();
    void DestroyEnvironmentResources();
    std::shared_ptr<const EnvironmentData> m_uploadedEnvironment;
    std::array<GpuTexture, 4> m_environmentTextures{};
    VkDescriptorSetLayout m_environmentSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_environmentSets{};

    std::vector<GpuTexture> m_materialTextures;
	std::vector<std::vector<VkDescriptorSet>> m_materialDescriptorSets;
	uint64_t m_materialLibraryIdentity = 0;
	uint64_t m_materialLibraryRevision = UINT64_MAX;

	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_commandBuffers;

	VkImage m_textureImage = VK_NULL_HANDLE;
	VmaAllocation m_textureImageAllocation = nullptr;
	VkImageView m_textureImageView = VK_NULL_HANDLE;
	VkSampler m_textureSampler = VK_NULL_HANDLE;
	uint32_t m_textureWidth = 0;
	uint32_t m_textureHeight = 0;
	uint32_t m_textureMipLevels = 1;

	// 由 vkAcquireNextImageKHR 发出信号，由 vkQueueSubmit 等待。每个正在处理的帧使用一个是安全的，因为该帧的栅栏会保护消费它的提交。
	std::vector<VkSemaphore> m_imageAvailableSemaphores;
	std::vector<VkFence> m_inFlightFences;

	// 每个 SWAPCHAIN 图像一个，而不是每个在飞的帧一个。看看 CreateRenderFinishedSemaphores()，就知道为什么这个区别很重要了。
	std::vector<VkSemaphore> m_renderFinishedSemaphores;

	uint32_t m_currentFrame = 0;

	VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;
	bool m_imguiInitialized = false;
    bool m_imguiContextCreated = false;
    bool m_imguiGlfwInitialized = false;

	const std::vector<const char*> validationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};
	const std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
};
