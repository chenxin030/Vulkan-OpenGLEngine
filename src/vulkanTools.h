#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

struct PushConstants
{
	glm::mat4 model;
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

class VulkanTools {
public:
	static constexpr uint32_t kMaxFramesInFlight = 2;

	VulkanTools(GLFWwindow* window) : m_window(window) {
		glfwSetWindowUserPointer(m_window, this);
	}
	~VulkanTools() { Cleanup(); }
	void Init();
	void Cleanup();
	void WaitIdle();
	void DrawFrame();

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

	void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

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

	VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
		uint32_t mipLevels) const;

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

	// resources sized to the swapchain
	VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
	SwapChainSupportDetails m_swapChainSupport;
	std::vector<VkImage> m_swapChainImages;
	VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D m_swapChainExtent{};
	uint32_t m_minImageCount = 0;
	std::vector<VkImageView> m_swapChainImageViews;
	std::vector<VkFramebuffer> m_swapChainFramebuffers;

	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_commandBuffers;

	// 由 vkAcquireNextImageKHR 发出信号，由 vkQueueSubmit 等待。每个正在处理的帧使用一个是安全的，因为该帧的栅栏会保护消费它的提交。
	std::vector<VkSemaphore> m_imageAvailableSemaphores;
	std::vector<VkFence> m_inFlightFences;

	// 每个 SWAPCHAIN 图像一个，而不是每个在飞的帧一个。看看 CreateRenderFinishedSemaphores()，就知道为什么这个区别很重要了。
	std::vector<VkSemaphore> m_renderFinishedSemaphores;

	uint32_t m_currentFrame = 0;

	const std::vector<const char*> validationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};
	const std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
};
