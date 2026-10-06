#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <string>

#include <glm/glm.hpp>

#include "../Renderer.h"
#include "scene/Vertex.h"
#include "scene/MeshLibrary.h"
#include "scene/Image.h"
#include <platform/FileWatch.h>

struct PushConstants
{
	glm::mat4 model;
};

struct UniformBufferObject {
	glm::mat4 view;
	glm::mat4 proj;
};

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
	virtual void DrawFrame(const Scene& scene) override;
	
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
	void CreateRenderPass();
	void CreateGraphicsPipeline();
	void CreateFramebuffers();

	void DestroySwapChainResources();
	void DestroyPipelineResources();
	void CleanupSwapChain();
	void RecreateSwapChain();
	// ----------------------------------------

	void CreateDescriptorSetLayout();
	void CreateUniformBuffers();
	void CreateDescriptorPool();
	void CreateDescriptorSets();
	void WriteTextureDescriptors();

	void CreateTextureImage();
	void GenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image,
		int32_t width, int32_t height, uint32_t mipLevels) const;
	void UploadTexture(const Image& image);
	void DestroyTextureResources();
	void ReloadTexture();
	void CreateTextureSampler(uint32_t mipLevels);

	// per frame
	// 幂等的"确保已上传"：每帧调用，自己判断有没有新条目。
	void EnsureMeshesUploaded(const MeshLibrary& meshes);
	void UploadMesh(const MeshData& mesh, uint32_t meshIndex);
	void UpdateUniformBuffer(uint32_t frameIndex, const Scene& scene);
	void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Scene& scene);

	// debug overlay
	void InitImGui();
	void InitImGuiVulkanBackend();
	void ShutdownImGui();
	void BuildImGuiFrame();

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

	void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
	void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

	void UploadToDeviceLocal(const void* source, VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkBuffer& buffer, VkDeviceMemory& bufferMemory);
	void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
	VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
		uint32_t mipLevels) const;
	void TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t baseMipLevel, uint32_t levelCount) const;
	void CopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
	
	void DestroyBuffer(VkBuffer& buffer, VkDeviceMemory& bufferMemory);
	void DestroyImage(VkImage& image, VkImageView& imageView, VkDeviceMemory& imageMemory, VkSampler& imageSampler);

	VkCommandBuffer BeginSingleTimeCommands();
	void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

	SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
	VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
	VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
	VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

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

	VkImage m_depthImage = VK_NULL_HANDLE;
	VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
	VkImageView m_depthImageView = VK_NULL_HANDLE;

	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

	// 场景资源。刻意与 swapchain 大小的那一组分开：这些都不依赖于尺寸，调整大小时不会影响它们。
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> m_descriptorSets;

	std::vector<VkBuffer> m_uniformBuffers;
	std::vector<VkDeviceMemory> m_uniformBuffersMemory;
	std::vector<void*> m_uniformBuffersMapped;

	FileWatch m_textureWatch;

	struct GpuMesh
	{
		VkBuffer vertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
		VkBuffer indexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;

		uint32_t indexCount = 0;
	};
	std::vector<GpuMesh> m_meshes;

	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_commandBuffers;

	VkImage m_textureImage = VK_NULL_HANDLE;
	VkDeviceMemory m_textureImageMemory;
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

	const std::vector<const char*> validationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};
	const std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
};
