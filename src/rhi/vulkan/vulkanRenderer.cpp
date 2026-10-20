#include "vulkanRenderer.h"

#include "scene/RenderSnapshot.h"
#include "core/ScopeExit.h"
#include "core/Profiling.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <map>
#include <cstring>
#include <set>
#include <algorithm>
#include <fstream>
#include <array>
#include <cassert>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <scene/MaterialLibrary.h>
#include <scene/Scene.h>

std::string TextureAssetPath()
{
    return std::string(ASSET_DIR) + "/checkerboard.ppm";
}

namespace {

// 纹理格式只在这里定义一次：上传、采样器、以及"能不能 blit"的能力查询必须问同一个格式。
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr uint32_t kDirectionalShadowMapSize = 1024;

}  // namespace

std::vector<char> ReadFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();

    return buffer;
}

VkVertexInputBindingDescription VertexBindingDescription()
{
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 5> VertexAttributeDescriptions()
{
    // 故意逐字段分配：offsetof 返回 size_t，把它转换为花括号初始化器里的 uint32_t `offset` 成员算是窄化转换，有些编译器会直接拒绝。
    std::array<VkVertexInputAttributeDescription, 5> attributes{};

    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = static_cast<uint32_t>(offsetof(Vertex, position));

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = static_cast<uint32_t>(offsetof(Vertex, color));

    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = static_cast<uint32_t>(offsetof(Vertex, texCoord));

    attributes[3].binding = 0;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[3].offset = static_cast<uint32_t>(offsetof(Vertex, normal));

    attributes[4].binding = 0;
    attributes[4].location = 4;
    attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[4].offset = static_cast<uint32_t>(offsetof(Vertex, tangent));
    return attributes;
}

void VulkanRenderer::ConfigureWindowHints() const
{
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
}

void VulkanRenderer::Init(GLFWwindow* window) {
    m_window = window;

    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateTimingQueries();

    CreateAllocator();

    CreateSwapChain();
    CreateImageViews();
    m_hdrFormat = FindSupportedFormat(
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    m_ldrFormat = FindSupportedFormat({VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

    CreateCommandPool();
    CreateCommandBuffers();

    CreateTextureImage();
    m_flatNormalTexture = UploadMaterialTexture(Image{1,1,{128,128,255,255},false});

    CreateUniformBuffers();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    CreateToneMapDescriptorSetLayout();
    CreateShadowResources();
    CreateDescriptorSets();
    CreateEnvironmentDescriptorSets();
    CreateRenderPass();
    CreateUiRenderPass();
    CreateGraphicsPipeline();
    CreateToneMapPipeline();
    CreateFramebuffers();

    CreateSyncObjects();

    InitImGui();
}

void VulkanRenderer::Cleanup() {
    // 正常退出由 Application 先 WaitIdle；部分 Init 回收时尚未进入异步帧循环。

    DestroyViewportTargets(m_viewportTargets);
    m_viewportExtent = {}; m_viewportImage = {};
    ShutdownImGui();

    for (auto semaphore : m_imageAvailableSemaphores)
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(m_device, semaphore, nullptr);
    for (auto fence : m_inFlightFences)
        if (fence != VK_NULL_HANDLE) vkDestroyFence(m_device, fence, nullptr);
    m_imageAvailableSemaphores.clear();
    m_inFlightFences.clear();
    // m_renderFinishedSemaphores 属于交换链，会在下面的 CleanupSwapChain() 中释放。

    if (m_timestampPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device, m_timestampPool, nullptr); m_timestampPool = VK_NULL_HANDLE;
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    CleanupSwapChain();

    DestroyTextureResources();
    DestroyMaterialTextures();
    DestroyImage(m_flatNormalTexture.image, m_flatNormalTexture.view,
        m_flatNormalTexture.allocation, m_flatNormalTexture.sampler);
    DestroyEnvironmentResources();

    for (GpuMesh& mesh : m_meshes) {
        DestroyBuffer(mesh.vertexBuffer, mesh.vertexAllocation);
        DestroyBuffer(mesh.indexBuffer, mesh.indexAllocation);
    }
    m_meshes.clear();

    for (size_t i = 0; i < m_uniformBuffers.size(); ++i) {
        DestroyBuffer(m_uniformBuffers[i], m_uniformBufferAllocations[i]);
    }
    m_uniformBuffers.clear();
    m_uniformBufferAllocations.clear();
    m_uniformBuffersMapped.clear();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    DestroyShadowResources();
    if (m_toneMapPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(m_device, m_toneMapPipeline, nullptr);
    if (m_toneMapPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device, m_toneMapPipelineLayout, nullptr);
    if (m_toneMapDescriptorSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_toneMapDescriptorSetLayout, nullptr);
    m_toneMapDescriptorSetLayout = VK_NULL_HANDLE;
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_environmentSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_environmentSetLayout, nullptr);
        m_environmentSetLayout = VK_NULL_HANDLE;
    }
    if (m_allocator != nullptr) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = nullptr;
    }

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_debugMessenger != VK_NULL_HANDLE) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    
}

void VulkanRenderer::WaitIdle()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}
void VulkanRenderer::DrawFrame(const RenderSnapshot& snapshot,
    const std::function<void()>& drawEditorUi) {

    ZoneScopedN("Vulkan.DrawFrameIncludingWaits");
    const auto renderStart = ProfileClock::now();
    m_metrics = {}; m_metrics.frame = ++m_frameSequence;
    ScopeExit renderTimer([&] { m_metrics.renderCpuMs = ProfileMilliseconds(renderStart); });
    // 轮询帧缓冲区大小而不是使用调整大小回调可以减少动用的部分，而且还能自动处理 DPI 变化。
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);

    if (width == 0 || height == 0) {
        // Application 统一等待；入口处尺寸仍可能变化，因此保留防御性跳帧。
        return;
    }

    if (static_cast<uint32_t>(width) != m_swapChainExtent.width ||
        static_cast<uint32_t>(height) != m_swapChainExtent.height) {
        RecreateSwapChain();
        return;
    }

    // 放在这一帧的最前面：ReloadTexture() 会 WaitIdle + 销毁重建纹理资源与描述符，
    // 绝不能发生在已经等过栅栏、或者已经开始录制之后。
    if (m_textureWatch.ConsumeChanged()) {
        ReloadTexture();
    }

    EnsureViewportTargets();
    m_viewportImage = {};
    m_viewportImage.status = m_viewportStatus;
    if (m_viewportRequest.visible && m_viewportRequest.extent.Valid() && m_viewportExtent.Valid())
        m_viewportImage = {uint64_t(m_viewportTargets[m_currentFrame].imguiSet), m_viewportExtent,
            {0, 0}, {1, 1}, m_viewportStatus};
    {
        ZoneScopedN("Vulkan.FenceWait");
        const auto start = ProfileClock::now();
        vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
        m_metrics.fenceMs = ProfileMilliseconds(start);
    }
    ReadTimingQueries();
    EnsureMaterialTexturesUploaded(*snapshot.materials);
    EnsureEnvironmentUploaded(snapshot.environmentData ? snapshot.environmentData : FallbackEnvironment());

    uint32_t imageIndex = 0;
    const auto acquireStart = ProfileClock::now();
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE, &imageIndex);
    m_metrics.acquireMs = ProfileMilliseconds(acquireStart);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapChain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    // 只有确定要提交工作时才重置fence。上面提前返回会让它保持信号状态。
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    UpdateUniformBuffer(m_currentFrame, snapshot);

    // 在录制之前先构建 UI，这样命令缓冲区就会使用这一帧的绘制数据。
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    BuildImGuiFrame(drawEditorUi);
    ImGui::Render();

    EnsureMeshesUploaded(*snapshot.meshes);

    RecordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, snapshot);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    const VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    const VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    const VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[imageIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    const VkSwapchainKHR swapChains[] = { m_swapChain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    {
        ZoneScopedN("Vulkan.Present");
        const auto start = ProfileClock::now();
        result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
        m_metrics.presentMs = ProfileMilliseconds(start);
    }
    m_metrics.submitted = m_viewportImage.Valid();

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        RecreateSwapChain();
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

void VulkanRenderer::CreateInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifndef NDEBUG
    if (!CheckValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    PopulateDebugMessengerCreateInfo(debugCreateInfo);
    // pNext是额外参数，指向扩展信息，里面通过sType指定类型。把配置借给驱动，换来一个临时的、拿不到句柄的 messenger；
    // vkCreateInstance 和 vkDestroyInstance 之间还没有 messenger，出错也没人能报错
    // 所以在 pNext 上挂一份临时的 messenger 配置，让层在instance创建到销毁中间的时间段也能报错。 
    // instance 建好之后，再用setupDebugMessenger() 再建一个持久的。
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
#else
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
#endif

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }
}

std::vector<const char*> VulkanRenderer::GetRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
#ifndef NDEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    return extensions;
}

bool VulkanRenderer::CheckValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

void VulkanRenderer::SetupDebugMessenger() {
#ifdef NDEBUG
    return;
#endif
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    PopulateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void VulkanRenderer::CreateSurface()
{
    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void VulkanRenderer::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    int highestScore = 0;
    // 先判断能不能用,再选一个最高分的
    for (const VkPhysicalDevice device : devices) {
        if (!IsDeviceSuitable(device)) {
            continue;
        }
        int score = RateDeviceSuitability(device);
        if (score > highestScore) {
            m_physicalDevice = device;
            highestScore = score;
        }
    }

    if (!m_physicalDevice) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    m_deviceName = properties.deviceName;
    std::cout << "[physical device]: " << m_deviceName << "           [score]: " << highestScore << std::endl;
}

void VulkanRenderer::CreateLogicalDevice() {
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    const std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    const float queuePriority = 1.0f;
    for (const uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    // 填好结构体 ≠ 打开了特性：**必须**把它交给 vkCreateDevice。
    // IsDeviceSuitable() 里查的是"设备支不支持"，这里才是"我要不要启用"。
    // 漏掉这一行，samplerAnisotropy 在逻辑设备上始终没开，
    // 后面 anisotropyEnable = VK_TRUE 就会被验证层判违规（VUID-...-01070）。
    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

#ifndef NDEBUG
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
#else
    createInfo.enabledLayerCount = 0;
#endif 
    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);
}

void VulkanRenderer::CreateSwapChain() {
    const SwapChainSupportDetails support = QuerySwapChainSupport(m_physicalDevice);
    const auto selectedPresentMode = ChooseSwapPresentMode(support.presentModes);
    m_deviceInfo.presentMode = selectedPresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" :
        selectedPresentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo";
    m_deviceInfo.presentationLimited = m_configuration.uncapped && selectedPresentMode != VK_PRESENT_MODE_IMMEDIATE_KHR;

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(support.presentModes);
    VkExtent2D extent = ChooseSwapExtent(support.capabilities);

    m_minImageCount = support.capabilities.minImageCount;

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    const uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        // 使用concurrent(并发)进行共享，两个不同的队列访问图像，不需要显式的所有权转移
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    // 跟随平台当前要求的变换，比如移动端可能是旋转90°
    createInfo.preTransform = support.capabilities.currentTransform;
    // 不透明，不和其他窗口混合
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    // The driver is allowed to create more images than requested: query again.
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;

    // m_renderFinishedSemaphores 与图像数量绑定，所以在这里创建，而不是在 CreateSyncObjects() 中：重新创建交换链可能会改变图像的数量。
    CreateRenderFinishedSemaphores();
}

void VulkanRenderer::CleanupSwapChain()
{
    DestroySwapChainResources();
    DestroyPipelineResources();
}

void VulkanRenderer::RecreateSwapChain() {
    // 最小化交回 Application 等待；这里不阻塞，关闭最小化窗口也能退出。
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    if (width == 0 || height == 0 || glfwWindowShouldClose(m_window)) return;

    WaitIdle();

    const VkFormat previousFormat = m_swapChainImageFormat;

    // 这里只销毁与交换链大小相关的资源（包括 renderFinished 信号量）。
    // 当格式不变时，渲染通道、管线会重复使用。
    DestroySwapChainResources();

    CreateSwapChain();
    CreateImageViews();
    if (m_swapChainImageFormat != previousFormat) {
        // 只重建 UI pass/backend；离屏格式和场景管线不依赖交换链。
        const bool hadImGuiBackend = m_imguiInitialized;
        if (hadImGuiBackend) {
            UnregisterViewportTextures(); // RemoveTexture 需要仍活着的 backend/pool。
            ImGui_ImplVulkan_Shutdown(); m_imguiInitialized = false;
        }
        if (m_uiRenderPass) vkDestroyRenderPass(m_device, m_uiRenderPass, nullptr);
        m_uiRenderPass = VK_NULL_HANDLE;
        CreateUiRenderPass();
        if (hadImGuiBackend) { InitImGuiVulkanBackend(); RegisterViewportTextures(); }
    }

    CreateFramebuffers();

    if (m_imguiInitialized) {
        ImGui_ImplVulkan_SetMinImageCount(m_minImageCount);
    }
}

void VulkanRenderer::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // 采样只发生在片元着色器里。stageFlags 留空（= 0）是个"沉默"的错：
    // 只要没有任何 shader 真的碰 binding 1，它就一直不报错 —— 一旦片元采样
    // 打开，vkCreateGraphicsPipelines 立刻判违规（VUID-...-layout-07988）。
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding shadowLayoutBinding{};
    shadowLayoutBinding.binding = 2;
    shadowLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowLayoutBinding.descriptorCount = 1;
    shadowLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    auto normalLayoutBinding = samplerLayoutBinding; normalLayoutBinding.binding = 3;
    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
        uboLayoutBinding, samplerLayoutBinding, shadowLayoutBinding, normalLayoutBinding };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

// 环境使用独立 set 1，一份场景资源由所有材质共享，避免每张 base-color 纹理复制四个环境 binding。
void VulkanRenderer::CreateEnvironmentDescriptorSets()
{
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t i = 0; i < 5; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = uint32_t(bindings.size()); layout.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(m_device, &layout, nullptr, &m_environmentSetLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create environment descriptor layout");
    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts{};
    layouts.fill(m_environmentSetLayout);
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = m_descriptorPool; info.descriptorSetCount = kMaxFramesInFlight; info.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(m_device, &info, m_environmentSets.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate environment descriptor sets");
    EnsureEnvironmentUploaded(FallbackEnvironment());
}

void VulkanRenderer::WriteEnvironmentDescriptors()
{
    for (uint32_t frame = 0; frame < kMaxFramesInFlight; ++frame) {
        std::array<VkDescriptorImageInfo, 4> images{};
        std::array<VkWriteDescriptorSet, 5> writes{};
        VkDescriptorBufferInfo buffer{m_uniformBuffers[frame], 0, sizeof(UniformBufferObject)};
        for (uint32_t i = 0; i < 5; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_environmentSets[frame]; writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            if (i < 4) {
                const auto& t = m_environmentTextures[i];
                images[i] = {t.sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo = &images[i];
            } else {
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[i].pBufferInfo = &buffer;
            }
        }
        vkUpdateDescriptorSets(m_device, uint32_t(writes.size()), writes.data(), 0, nullptr);
    }
}

VulkanRenderer::GpuTexture VulkanRenderer::UploadFloatTexture(const FloatTexture& texture)
{
    constexpr VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
    const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("environment RGBA32F textures require linear sampling support");
    const bool cube = texture.layers == 6;
    VkImageFormatProperties capabilities{};
    const auto usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const auto flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : VkImageCreateFlags(0);
    const auto& base = texture.mips.front();
    if (vkGetPhysicalDeviceImageFormatProperties(m_physicalDevice, format, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, usage, flags, &capabilities) != VK_SUCCESS ||
        capabilities.maxArrayLayers < texture.layers || capabilities.maxMipLevels < texture.mips.size() ||
        capabilities.maxExtent.width < base.width || capabilities.maxExtent.height < base.height)
        throw std::runtime_error("device cannot provide the requested environment texture");
    GpuTexture uploaded;
    VkBuffer staging = VK_NULL_HANDLE; VmaAllocation stagingAllocation = nullptr;
    try {
        size_t bytes = 0; for (const auto& mip : texture.mips) bytes += mip.pixels.size() * sizeof(float);
        VmaAllocationInfo stagingInfo{};
        CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::HostVisible, staging, stagingAllocation, &stagingInfo);
        std::vector<VkBufferImageCopy> regions;
        size_t offset = 0;
        for (uint32_t mip = 0; mip < texture.mips.size(); ++mip) {
            const auto& level = texture.mips[mip]; const auto size = level.pixels.size() * sizeof(float);
            std::memcpy(static_cast<uint8_t*>(stagingInfo.pMappedData) + offset, level.pixels.data(), size);
            VkBufferImageCopy region{}; region.bufferOffset = offset;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, texture.layers};
            region.imageExtent = {level.width, level.height, 1}; regions.push_back(region); offset += size;
        }
        vmaFlushAllocation(m_allocator, stagingAllocation, 0, bytes);
        CreateImage(base.width, base.height, uint32_t(texture.mips.size()), format, VK_IMAGE_TILING_OPTIMAL,
            usage, uploaded.image, uploaded.allocation, texture.layers, flags);
        const auto command = BeginSingleTimeCommands();
        TransitionImageLayout(command, uploaded.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, uint32_t(texture.mips.size()), texture.layers);
        vkCmdCopyBufferToImage(command, staging, uploaded.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            uint32_t(regions.size()), regions.data());
        TransitionImageLayout(command, uploaded.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            0, uint32_t(texture.mips.size()), texture.layers);
        EndSingleTimeCommands(command);
        DestroyBuffer(staging, stagingAllocation);
        uploaded.view = CreateImageView(uploaded.image, format, VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(texture.mips.size()), cube);
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = float(texture.mips.size() - 1);
        if (vkCreateSampler(m_device, &sampler, nullptr, &uploaded.sampler) != VK_SUCCESS)
            throw std::runtime_error("failed to create environment sampler");
    } catch (...) {
        DestroyBuffer(staging, stagingAllocation);
        DestroyImage(uploaded.image, uploaded.view, uploaded.allocation, uploaded.sampler);
        throw;
    }
    return uploaded;
}

void VulkanRenderer::EnsureEnvironmentUploaded(std::shared_ptr<const EnvironmentData> data)
{
    if (m_uploadedEnvironment == data) return;
    std::array<GpuTexture, 4> uploaded{};
    try {
        uploaded[0] = UploadFloatTexture(data->radiance);
        uploaded[1] = UploadFloatTexture(data->irradiance);
        uploaded[2] = UploadFloatTexture(data->prefiltered);
        uploaded[3] = UploadFloatTexture(*data->brdfLut);
    } catch (...) {
        for (auto& t : uploaded) DestroyImage(t.image, t.view, t.allocation, t.sampler);
        throw;
    }
    // 替换纹理及描述符前等待所有飞行帧；四张只读纹理可以跨帧共享。
    WaitIdle(); DestroyEnvironmentResources();
    m_environmentTextures = uploaded; m_uploadedEnvironment = std::move(data);
    WriteEnvironmentDescriptors();
}

void VulkanRenderer::DestroyEnvironmentResources()
{
    for (auto& t : m_environmentTextures) DestroyImage(t.image, t.view, t.allocation, t.sampler);
    m_uploadedEnvironment.reset();
}

void VulkanRenderer::CreateUniformBuffers()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    m_uniformBuffers.resize(kMaxFramesInFlight);
    m_uniformBufferAllocations.resize(kMaxFramesInFlight);
    m_uniformBuffersMapped.resize(kMaxFramesInFlight);

    for (size_t i = 0; i < kMaxFramesInFlight; i++) {
        VmaAllocationInfo allocationInfo{};
        CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUse::HostVisible, m_uniformBuffers[i], m_uniformBufferAllocations[i], &allocationInfo);

        // 映射一次后再也不取消映射。旧代码调用 vkMapMemory 来实现相同的效果；
        // 使用 VMA 时，映射在创建时就请求好了，指针可以直接从分配信息中读出。
        m_uniformBuffersMapped[i] = allocationInfo.pMappedData;
    }
}

void VulkanRenderer::CreateDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constexpr uint32_t maxMaterialTextures = 256; // 每个材质一个 set，三种采样 binding。
    poolSizes[0].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight) * (2 + maxMaterialTextures);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight) * (7 + 3 * maxMaterialTextures);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[2].descriptorCount = 2 * kMaxFramesInFlight; // 活动目标和 resize 候选并存。

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(kMaxFramesInFlight) * (2 + maxMaterialTextures) + 2 * kMaxFramesInFlight;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void VulkanRenderer::CreateToneMapDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding inputBinding{};
    inputBinding.binding = 0;
    inputBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    inputBinding.descriptorCount = 1;
    inputBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &inputBinding;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
        &m_toneMapDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create tone-map descriptor layout");

}

void VulkanRenderer::CreateDescriptorSets() 
{
    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(kMaxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(kMaxFramesInFlight);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < kMaxFramesInFlight; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_textureImageView;
        imageInfo.sampler = m_textureSampler;

        VkDescriptorImageInfo shadowInfo{ m_shadowSampler, m_shadowImageViews[i],
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

        VkDescriptorImageInfo normalInfo{m_flatNormalTexture.sampler, m_flatNormalTexture.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = m_descriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &shadowInfo;

        descriptorWrites[3] = descriptorWrites[1];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].pImageInfo = &normalInfo;
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void VulkanRenderer::WriteTextureDescriptors()
{
    // 从 CreateDescriptorSets() 里拆出来，因为热重载只需要重写 binding 1，
    // binding 0（每帧不同的 UBO）不许碰。
    //
    // 这里是**更新**已有的 set，而不是重新分配。这不是风格问题：池是按 kMaxFramesInFlight
    // 精确分配的（每种描述符各 2 个、maxSets 也是 2），每次重载都重分配的话两次就把池用干，
    // 失败在 vkAllocateDescriptorSets —— 而那个点离原因很远。
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_textureImageView;
        imageInfo.sampler = m_textureSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_descriptorSets[i];
        write.dstBinding = 1;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
}

VkResult VulkanRenderer::CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) {

    // vkCreateDebugUtilsMessengerEXT 是 VK_EXT_debug_utils 的扩展函数，这个扩展函数要在运行时取地址。
    // 前面 createInstance 配置了临时的messenger挂进pNext，而 setupDebugMessenger 创建的 VkDebugUtilsMessengerEXT 对象是持久的
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    }

    return VK_FALSE;
}

void VulkanRenderer::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

void VulkanRenderer::DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device) {

    QueueFamilyIndices indices = FindQueueFamilies(device);
    bool extensionsSupported = CheckDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        const SwapChainSupportDetails support = QuerySwapChainSupport(device);
        swapChainAdequate = !support.formats.empty() && !support.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

int VulkanRenderer::RateDeviceSuitability(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    int score = 0;

    // 独立显卡有明显的性能优势
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }

    // 纹理的最大可能尺寸会影响图像质量
    score += deviceProperties.limits.maxImageDimension2D;

    if (!deviceFeatures.geometryShader) {
        return 0;
    }

    return score;
}

QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }
    }

    return indices;
}

bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

SwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const VkSurfaceFormatKHR& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) const
{
    if (m_configuration.uncapped)
        for (const auto mode : availablePresentModes) if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) return mode;
    // Mailbox / FIFO remain available when immediate presentation is unsupported.
    for (const VkPresentModeKHR availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actualExtent.width = std::clamp(actualExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);

    return actualExtent;
}

void VulkanRenderer::CreateImageViews() {
    m_swapChainImageViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views!");
        }
    }
}

void VulkanRenderer::UnregisterViewportTextures()
{
    // 退出/格式变化前先解除注册；backend 的布局与描述符池必须仍活着。
    for (auto& target : m_viewportTargets) {
        if (target.imguiSet && m_imguiInitialized) ImGui_ImplVulkan_RemoveTexture(target.imguiSet);
        target.imguiSet = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::RegisterViewportTextures()
{
    for (auto& target : m_viewportTargets) {
        if (target.ldrView && !target.imguiSet)
            target.imguiSet = ImGui_ImplVulkan_AddTexture(target.sampler, target.ldrView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void VulkanRenderer::DestroyViewportTargets(ViewportTargets& targets)
{
    // 调用者保证 GPU 已结束使用；候选分配失败时从未提交，不需要再次 WaitIdle。
    for (auto& target : targets) {
        if (target.imguiSet && m_imguiInitialized) ImGui_ImplVulkan_RemoveTexture(target.imguiSet);
        if (target.toneSet && m_descriptorPool)
            vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &target.toneSet);
        if (target.framebuffer) vkDestroyFramebuffer(m_device, target.framebuffer, nullptr);
        if (target.sampler) vkDestroySampler(m_device, target.sampler, nullptr);
        if (target.ldrView) vkDestroyImageView(m_device, target.ldrView, nullptr);
        if (target.hdrView) vkDestroyImageView(m_device, target.hdrView, nullptr);
        if (target.depthView) vkDestroyImageView(m_device, target.depthView, nullptr);
        if (target.ldr) vmaDestroyImage(m_allocator, target.ldr, target.ldrAllocation);
        if (target.hdr) vmaDestroyImage(m_allocator, target.hdr, target.hdrAllocation);
        if (target.depth) vmaDestroyImage(m_allocator, target.depth, target.depthAllocation);
        target = {};
    }
}

void VulkanRenderer::EnsureViewportTargets()
{
    if (!m_viewportRequest.visible || !m_viewportRequest.extent.Valid()) return;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    const auto& limits = properties.limits;

    const ViewportExtent wanted{
        std::min(m_viewportRequest.extent.width, std::min({limits.maxImageDimension2D,
            limits.maxFramebufferWidth, limits.maxViewportDimensions[0]})),
        std::min(m_viewportRequest.extent.height, std::min({limits.maxImageDimension2D,
            limits.maxFramebufferHeight, limits.maxViewportDimensions[1]}))};
    if (wanted == m_viewportExtent) { 
        m_failedViewportExtent = {}; 
        m_viewportStatus.clear(); 
        return; 
    }
    if (wanted == m_failedViewportExtent) return;

    ViewportTargets candidate{};
    ScopeExit rollback([&] { DestroyViewportTargets(candidate); });

    try {
        for (auto& target : candidate) {
            CreateImage(wanted.width, wanted.height, 1, m_hdrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                target.hdr, target.hdrAllocation);
            target.hdrView = CreateImageView(target.hdr, m_hdrFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

            const VkFormat depthFormat = FindDepthFormat();
            CreateImage(wanted.width, wanted.height, 1, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, target.depth, target.depthAllocation);
            target.depthView = CreateImageView(target.depth, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

            CreateImage(wanted.width, wanted.height, 1, m_ldrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                target.ldr, target.ldrAllocation);
            target.ldrView = CreateImageView(target.ldr, m_ldrFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

            VkSamplerCreateInfo sampler{}; 
            sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            if (vkCreateSampler(m_device, &sampler, nullptr, &target.sampler) != VK_SUCCESS)
                throw std::runtime_error("failed to create viewport sampler");

            const std::array<VkImageView, 3> views{target.ldrView, target.hdrView, target.depthView};

            VkFramebufferCreateInfo framebuffer{}; framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer.renderPass = m_renderPass; framebuffer.attachmentCount = uint32_t(views.size());
            framebuffer.pAttachments = views.data(); framebuffer.width = wanted.width;
            framebuffer.height = wanted.height; framebuffer.layers = 1;
            if (vkCreateFramebuffer(m_device, &framebuffer, nullptr, &target.framebuffer) != VK_SUCCESS)
                throw std::runtime_error("failed to create viewport framebuffer");

            VkDescriptorSetAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocation.descriptorPool = m_descriptorPool; allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &m_toneMapDescriptorSetLayout;
            if (vkAllocateDescriptorSets(m_device, &allocation, &target.toneSet) != VK_SUCCESS)
                throw std::runtime_error("failed to allocate viewport tone-map descriptor");

            VkDescriptorImageInfo hdr{}; hdr.imageView = target.hdrView;
            hdr.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{}; write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = target.toneSet; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT; write.pImageInfo = &hdr;
            vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
            target.imguiSet = ImGui_ImplVulkan_AddTexture(target.sampler, target.ldrView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (!target.imguiSet) throw std::runtime_error("failed to register viewport texture");
        }
        // 候选完全成功后才发布；所有旧槽位的 UI 采样完成后才能解除注册/销毁。
        WaitIdle();
        m_viewportTargets.swap(candidate);
        m_viewportExtent = wanted;
        m_failedViewportExtent = {}; m_viewportStatus.clear();
    } catch (const std::exception& error) {
        // 相同失败请求不会每帧重新分配；改尺寸后再尝试，旧目标继续等比例显示。
        m_failedViewportExtent = wanted; m_viewportStatus = error.what();
    }
}

void VulkanRenderer::CreateShadowResources()
{
    m_shadowDepthFormat = FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    for (uint32_t frame = 0; frame < kMaxFramesInFlight; ++frame) {
        CreateImage(kDirectionalShadowMapSize, kDirectionalShadowMapSize, 1, m_shadowDepthFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            m_shadowImages[frame], m_shadowImageAllocations[frame]);
        m_shadowImageViews[frame] = CreateImageView(m_shadowImages[frame], m_shadowDepthFormat,
            VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS)
        throw std::runtime_error("failed to create directional shadow sampler");

    CreateShadowRenderPass();
    for (uint32_t frame = 0; frame < kMaxFramesInFlight; ++frame) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_shadowRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &m_shadowImageViews[frame];
        framebufferInfo.width = kDirectionalShadowMapSize;
        framebufferInfo.height = kDirectionalShadowMapSize;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr,
            &m_shadowFramebuffers[frame]) != VK_SUCCESS)
            throw std::runtime_error("failed to create directional shadow framebuffer");
    }
    CreateShadowPipeline();
}

void VulkanRenderer::CreateShadowRenderPass()
{
    VkAttachmentDescription depth{};
    depth.format = m_shadowDepthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &depth;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    if (vkCreateRenderPass(m_device, &info, nullptr, &m_shadowRenderPass) != VK_SUCCESS)
        throw std::runtime_error("failed to create directional shadow render pass");
}

void VulkanRenderer::CreateShadowPipeline()
{
    const auto vertexCode = ReadFile(std::string(SHADER_DIR) + "/shadow.vert.spv");
    const auto fragmentCode = ReadFile(std::string(SHADER_DIR) + "/shadow.frag.spv");
    const VkShaderModule vertexModule = CreateShaderModule(vertexCode);
    ScopeExit vertexGuard([&] { vkDestroyShaderModule(m_device, vertexModule, nullptr); });
    const VkShaderModule fragmentModule = CreateShaderModule(fragmentCode);
    ScopeExit fragmentGuard([&] { vkDestroyShaderModule(m_device, fragmentModule, nullptr); });
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    const VkVertexInputBindingDescription binding = VertexBindingDescription();
    VkVertexInputAttributeDescription position{};
    position.binding = 0;
    position.location = 0;
    position.format = VK_FORMAT_R32G32B32_SFLOAT;
    position.offset = static_cast<uint32_t>(offsetof(Vertex, position));
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &position;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_TRUE;
    raster.depthBiasConstantFactor = 1.25f;
    raster.depthBiasSlopeFactor = 1.75f;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkDescriptorSetLayout setLayout = m_descriptorSetLayout;
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(glm::mat4);
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &setLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(m_device, &layout, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create directional shadow pipeline layout");

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = m_shadowPipelineLayout;
    info.renderPass = m_shadowRenderPass;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &m_shadowPipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create directional shadow pipeline");
}

void VulkanRenderer::DestroyShadowResources()
{
    if (m_shadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
    if (m_shadowPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device, m_shadowPipelineLayout, nullptr);
    for (VkFramebuffer framebuffer : m_shadowFramebuffers)
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    if (m_shadowRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_shadowRenderPass, nullptr);
    if (m_shadowSampler != VK_NULL_HANDLE) vkDestroySampler(m_device, m_shadowSampler, nullptr);
    for (uint32_t frame = 0; frame < kMaxFramesInFlight; ++frame) {
        if (m_shadowImageViews[frame] != VK_NULL_HANDLE)
            vkDestroyImageView(m_device, m_shadowImageViews[frame], nullptr);
        if (m_shadowImages[frame] != VK_NULL_HANDLE)
            vmaDestroyImage(m_allocator, m_shadowImages[frame], m_shadowImageAllocations[frame]);
    }
    m_shadowPipeline = VK_NULL_HANDLE;
    m_shadowPipelineLayout = VK_NULL_HANDLE;
    m_shadowFramebuffers.fill(VK_NULL_HANDLE);
    m_shadowRenderPass = VK_NULL_HANDLE;
    m_shadowSampler = VK_NULL_HANDLE;
    m_shadowImageViews.fill(VK_NULL_HANDLE);
    m_shadowImages.fill(VK_NULL_HANDLE);
    m_shadowImageAllocations.fill(nullptr);
}

void VulkanRenderer::RecordShadowPass(VkCommandBuffer commandBuffer, const RenderSnapshot& snapshot)
{
    VkClearValue clear{};
    clear.depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = m_shadowRenderPass;
    pass.framebuffer = m_shadowFramebuffers[m_currentFrame];
    pass.renderArea.offset = { 0, 0 };
    pass.renderArea.extent = { kDirectionalShadowMapSize, kDirectionalShadowMapSize };
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
    const VkDescriptorSet descriptorSet = m_descriptorSets[m_currentFrame];
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_shadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    VkViewport viewport{};
    viewport.width = static_cast<float>(kDirectionalShadowMapSize);
    viewport.height = static_cast<float>(kDirectionalShadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = { kDirectionalShadowMapSize, kDirectionalShadowMapSize };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    for (const DrawItem& item : snapshot.drawItems) {
        if (item.unlit) continue;
        const GpuMesh& mesh = m_meshes.at(item.meshIndex);
        const VkBuffer vertexBuffer = mesh.vertexBuffer;
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(commandBuffer, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(glm::mat4), &item.worldTransform);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
        if (m_configuration.transport) { ++m_metrics.shadowDraws; m_metrics.shadowTriangles += mesh.indexCount / 3; }
    }
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanRenderer::CreateGraphicsPipeline()
{
    const std::vector<char> vertShaderCode = ReadFile(std::string(SHADER_DIR) + "/mesh.vert.spv");
    const std::vector<char> fragShaderCode = ReadFile(std::string(SHADER_DIR) + "/mesh.frag.spv");

    const VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
    ScopeExit vertexGuard([&] { vkDestroyShaderModule(m_device, vertShaderModule, nullptr); });
    const VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);
    ScopeExit fragmentGuard([&] { vkDestroyShaderModule(m_device, fragShaderModule, nullptr); });

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    const VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    const VkVertexInputBindingDescription bindingDescription = VertexBindingDescription();
    const std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions = VertexAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 视口/裁剪是动态变化的，所以管线只声明数量。
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    const std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // 管线布局必须声明与着色器在 set 0（每帧相机缓冲区）绑定的相同 set 布局。如果不匹配，会导致管线创建失败。

    // model/material push constants 被顶点和片元阶段读取，因此两阶段都要声明。
    // 搞错了的话不会在管线创建时失败，而会在绘制时失败。
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const VkDescriptorSetLayout meshLayouts[] = {m_descriptorSetLayout, m_environmentSetLayout};
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = meshLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    // 静态绕序的第二条管线，不要求扩展动态状态；模型反射仍保持背面剔除。
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_mirroredPipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create mirrored mesh pipeline");
}

void VulkanRenderer::CreateToneMapPipeline()
{
    const auto vertexCode = ReadFile(std::string(SHADER_DIR) + "/tonemap.vert.spv");
    const auto fragmentCode = ReadFile(std::string(SHADER_DIR) + "/tonemap.frag.spv");
    const VkShaderModule vertex = CreateShaderModule(vertexCode);
    ScopeExit vertexGuard([&] { vkDestroyShaderModule(m_device, vertex, nullptr); });
    const VkShaderModule fragment = CreateShaderModule(fragmentCode);
    ScopeExit fragmentGuard([&] { vkDestroyShaderModule(m_device, fragment, nullptr); });

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_FALSE;
    depth.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const VkDescriptorSetLayout toneLayouts[] = {m_toneMapDescriptorSetLayout, m_environmentSetLayout};
    layout.setLayoutCount = 2;
    layout.pSetLayouts = toneLayouts;
    if (vkCreatePipelineLayout(m_device, &layout, nullptr, &m_toneMapPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create tone-map pipeline layout");

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = m_toneMapPipelineLayout;
    info.renderPass = m_renderPass;
    info.subpass = 1;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr,
        &m_toneMapPipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create tone-map pipeline");
}

void VulkanRenderer::CreateFramebuffers()
{
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i) {
        std::array<VkImageView, 1> attachments = {m_swapChainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_uiRenderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

// ---------------------------------------------------------------------------
// Recreating the swapchain
//
// 交换链重建只影响呈现 framebuffer/UI pass；离屏目标和场景管线独立于窗口大小/格式。
// ---------------------------------------------------------------------------

void VulkanRenderer::DestroySwapChainResources()
{
    DestroyRenderFinishedSemaphores();

    for (const VkFramebuffer framebuffer : m_swapChainFramebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_swapChainFramebuffers.clear();

    for (const VkImageView imageView : m_swapChainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapChainImageViews.clear();

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
    m_swapChainImages.clear();
}

void VulkanRenderer::DestroyPipelineResources()
{
    if (m_mirroredPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_mirroredPipeline, nullptr);
        m_mirroredPipeline = VK_NULL_HANDLE;
    }
    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_toneMapPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_toneMapPipeline, nullptr);
        m_toneMapPipeline = VK_NULL_HANDLE;
    }
    if (m_toneMapPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_toneMapPipelineLayout, nullptr);
        m_toneMapPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_uiRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_uiRenderPass, nullptr);
        m_uiRenderPass = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Command buffers and synchronisation
// ---------------------------------------------------------------------------

void VulkanRenderer::CreateCommandPool() {
    const QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // RESET_COMMAND_BUFFER 每帧重复记录相同的缓冲
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void VulkanRenderer::CreateCommandBuffers()
{
    m_commandBuffers.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void VulkanRenderer::CreateSyncObjects()
{
    m_imageAvailableSemaphores.resize(kMaxFramesInFlight);
    m_inFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // 开始发出信号，否则第一个帧会一直阻塞。
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

// 为什么每个交换链图像使用一个 renderFinished 信号量，而不是每个正在飞行的帧一个：
// renderFinished 信号量是由 vkQueueSubmit 发送信号，但被 vkQueuePresentKHR 等待。
// present 不会被 fence 保护，所以没有什么能告诉我们 present 引擎什么时候完成了读取这个信号量。
// 比如说有 3 个交换链图像，但只有 2 个帧在飞行中，那么一个信号量可能在它之前的显示还在挂起时就被再次发送信号——VUID - vkQueueSubmit - pSignalSemaphores - 00067 明确禁止。
// 通过按获取的图像索引来索引就解决了这个问题：能够重新获取图像 i 本身就证明图像 i 的前一次显示已经完成，所以与图像 i 配对的信号量在再次发送信号时一定是未发送状态。
void VulkanRenderer::CreateRenderFinishedSemaphores()
{
    m_renderFinishedSemaphores.resize(m_swapChainImages.size());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (VkSemaphore& semaphore : m_renderFinishedSemaphores) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("failed to create a render-finished semaphore!");
        }
    }
}

void VulkanRenderer::DestroyRenderFinishedSemaphores()
{
    for (const VkSemaphore semaphore : m_renderFinishedSemaphores) {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    m_renderFinishedSemaphores.clear();
}

void VulkanRenderer::CreateTextureImage()
{
    const std::string path = TextureAssetPath();
    const Image image = LoadImage(path);
    if (!image.IsValid()) {
        throw std::runtime_error("decoded image is empty: " + path);
    }

    UploadTexture(image);

    // 基线必须在首次加载成功之后才打：打早了，第一帧的 ConsumeChanged()
    // 就会为刚刚加载的这个文件返回 true，白白重载一次。
    m_textureWatch.Reset(path);
}

void VulkanRenderer::GenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image,
    int32_t width, int32_t height, uint32_t mipLevels) const
{
    if (mipLevels <= 1) {
        return;
    }

    // 除最后一级外每个级别都以 SHADER_READ_ONLY_OPTIMAL 结束此功能。
    // 级别 0 在这里以 TRANSFER_DST 状态到达（调用方会把暂存缓冲区复制进来），最后一个级别则保留为一个从未被读取过的普通目标。
    int32_t sourceWidth = width;
    int32_t sourceHeight = height;

    for (uint32_t level = 1; level < mipLevels; ++level) {
        // 上一级变成 blit 源 ……
        TransitionImageLayout(commandBuffer, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            level - 1, 1);

        // ……而这一层变成目标。
        // 在指定 blit 的 TRANSFER_DST_OPTIMAL 时，让目标保持 UNDEFINED 状态。它通常能侥幸通过。这不是必须侥幸通过的情况。
        TransitionImageLayout(commandBuffer, image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            level, 1);

        const int32_t destinationWidth = sourceWidth > 1 ? sourceWidth / 2 : 1;
        const int32_t destinationHeight = sourceHeight > 1 ? sourceHeight / 2 : 1;

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { sourceWidth, sourceHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { destinationWidth, destinationHeight, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        // 两边都是相同的图像，不同的 mip 级别：在不进行第二次分配的情况下进行原地下采样的标准方法。
        vkCmdBlitImage(commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // 源层现在已经完全读取完毕，所以它可以进入最终布局，同时循环继续进行。
        TransitionImageLayout(commandBuffer, image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            level - 1, 1);

        sourceWidth = destinationWidth;
        sourceHeight = destinationHeight;
    }
}

void VulkanRenderer::UploadTexture(const Image& image)
{
    // 数据纹理不得经过 sRGB 解码；能力查询、image 和 view 使用相同格式。
    const VkFormat textureFormat = image.srgb ? kTextureFormat : VK_FORMAT_R8G8B8A8_UNORM;
    m_textureWidth = image.width;
    m_textureHeight = image.height;

    // 完整 mip 链直到 1x1：最长边的 log2，加上基础层。
    m_textureMipLevels = static_cast<uint32_t>(std::floor(
        std::log2(static_cast<double>(std::max(image.width, image.height))))) + 1;

    // 规范并不保证 mip 级别之间的线性 blit：一个格式能采样，却可能拒绝当 blit 的源或目标。
    // 先问，而不是在某些厂商驱动上发布一张全黑的贴图。
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, textureFormat, &formatProperties);

    const VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures) {
        throw std::runtime_error(
            "the texture format lacks blit or linear-filter support on this device, "
            "so a mip chain cannot be generated");
    }

    VkImageFormatProperties imageFormatProperties{};
    if (vkGetPhysicalDeviceImageFormatProperties(
        m_physicalDevice, textureFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT,
        0, &imageFormatProperties) != VK_SUCCESS ||
        imageFormatProperties.maxMipLevels < m_textureMipLevels) {
        throw std::runtime_error(
            "the device cannot provide the requested mip chain for the texture format");
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(image.width) *
        static_cast<VkDeviceSize>(image.height) * 4;

    // 像素从主机内存来，所以上 GPU 的路和顶点/索引缓冲一样：staging 两段式。
    VkBuffer stagingBuffer = VK_NULL_HANDLE;    
    VmaAllocation stagingAllocation = nullptr;
    VmaAllocationInfo stagingInfo{};
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::HostVisible,
        stagingBuffer, stagingAllocation, &stagingInfo);
    ScopeExit stagingGuard([&] { DestroyBuffer(stagingBuffer, stagingAllocation); });

    std::memcpy(stagingInfo.pMappedData, image.pixels.data(), static_cast<size_t>(imageSize));
    vmaFlushAllocation(m_allocator, stagingAllocation, 0, imageSize);

    // SRGB 而不是 UNORM：采样器在读取时把 sRGB 转成线性，交换链在写出时再转回去。
    // 这一对弄错就是"贴图发白/发灰"的经典原因。
    //
    // TRANSFER_SRC 和 TRANSFER_DST 都要：mip 链是在同一张 image 内部逐层 blit 出来的。
    CreateImage(image.width, image.height, m_textureMipLevels, textureFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT,
        m_textureImage, m_textureImageAllocation);

    // 整次上传只用一条命令缓冲：两次布局转换 + 缓冲拷贝 + 每个 mip 的 blit。
    const VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    TransitionImageLayout(commandBuffer, m_textureImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1);
    CopyBufferToImage(commandBuffer, stagingBuffer, m_textureImage,
        static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height));
    GenerateMipmaps(commandBuffer, m_textureImage,
        static_cast<int32_t>(image.width), static_cast<int32_t>(image.height),
        m_textureMipLevels);
    TransitionImageLayout(commandBuffer, m_textureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        m_textureMipLevels - 1, 1);

    EndSingleTimeCommands(commandBuffer);

    DestroyBuffer(stagingBuffer, stagingAllocation);

    // view 要覆盖整条链，否则采样器只能读到 level 0，生成的 mip 对它不可见。
    m_textureImageView = CreateImageView(m_textureImage, textureFormat,
        VK_IMAGE_ASPECT_COLOR_BIT, m_textureMipLevels);

    CreateTextureSampler(m_textureMipLevels);
}

void VulkanRenderer::DestroyTextureResources()
{
    DestroyImage(m_textureImage, m_textureImageView, m_textureImageAllocation, m_textureSampler);

    m_textureWidth = 0;
    m_textureHeight = 0;
    m_textureMipLevels = 1;
}

VulkanRenderer::GpuTexture VulkanRenderer::UploadMaterialTexture(const Image& image)
{
    // 保存默认棋盘状态；重复使用相同的临时、mip 和采样器路径
    GpuTexture saved{ m_textureImage,m_textureImageAllocation,m_textureImageView,m_textureSampler };
    const uint32_t width = m_textureWidth, height = m_textureHeight, levels = m_textureMipLevels;
    auto restore=[&] {
        m_textureImage=saved.image; 
        m_textureImageAllocation=saved.allocation;
        m_textureImageView=saved.view; 
        m_textureSampler=saved.sampler;
        m_textureWidth=width; 
        m_textureHeight=height; 
        m_textureMipLevels=levels;
    };
    m_textureImage=VK_NULL_HANDLE; 
    m_textureImageAllocation=nullptr;
    m_textureImageView=VK_NULL_HANDLE; 
    m_textureSampler=VK_NULL_HANDLE;
    try {
        UploadTexture(image);
        GpuTexture result{ m_textureImage,m_textureImageAllocation,m_textureImageView,m_textureSampler };
        restore(); return result;
    } catch (...) { DestroyTextureResources(); restore(); throw; }
}

void VulkanRenderer::EnsureMaterialTexturesUploaded(const MaterialLibrary& materials)
{
    if (m_materialLibraryIdentity == materials.Identity() &&
        m_materialLibraryRevision == materials.Revision()) return;
    if (materials.TextureCount() > 128 || materials.MaterialCount() > 256)
        throw std::runtime_error("Vulkan scene limit is 128 images and 256 materials");
    WaitIdle();
    DestroyMaterialTextures();
    for (uint32_t i=0;i<materials.TextureCount();++i)
        m_materialTextures.push_back(UploadMaterialTexture(materials.GetTexture(i)));
    // 每个材质绑定自己的颜色/法线组合，共用颜色图不再共用错误的法线 binding。
    for (uint32_t i=0;i<materials.MaterialCount();++i) {
        const auto& material = materials.GetMaterial(i);
        std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight,m_descriptorSetLayout);
        std::vector<VkDescriptorSet> sets(kMaxFramesInFlight);
        VkDescriptorSetAllocateInfo ai{};
        ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; 
        ai.descriptorPool=m_descriptorPool;
        ai.descriptorSetCount=kMaxFramesInFlight; 
        ai.pSetLayouts=layouts.data();
        if (vkAllocateDescriptorSets(m_device,&ai,sets.data())!=VK_SUCCESS)
            throw std::runtime_error("failed to allocate material descriptor sets");

        m_materialDescriptorSets.push_back(sets);
        for (uint32_t frame=0;frame<kMaxFramesInFlight;++frame) {
            const auto& normal = material.normalTexture == UINT32_MAX ? m_flatNormalTexture : m_materialTextures.at(material.normalTexture);
            VkDescriptorBufferInfo buffer{ m_uniformBuffers[frame],0,sizeof(UniformBufferObject) };
            VkDescriptorImageInfo base{ m_textureSampler,m_textureImageView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            if (material.baseColorTexture != UINT32_MAX) {
                const auto& t = m_materialTextures.at(material.baseColorTexture);
                base = { 
                    t.sampler,
                    t.view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 
                };
            }
            std::array<VkDescriptorImageInfo, 3> images{ {base,
                {m_shadowSampler,m_shadowImageViews[frame],VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
                {normal.sampler,normal.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}} };
            std::array<VkWriteDescriptorSet, 4> writes{};
            for (uint32_t k=0;k<4;++k) {
                writes[k].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; 
                writes[k].dstSet=sets[frame];
                writes[k].dstBinding=k; 
                writes[k].descriptorCount=1;
                writes[k].descriptorType = k == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                if (k == 0) 
                    writes[k].pBufferInfo = &buffer; 
                else 
                    writes[k].pImageInfo = &images[k - 1];
            }
            vkUpdateDescriptorSets(m_device,4,writes.data(),0,nullptr);
        }
    }
    m_materialLibraryIdentity=materials.Identity(); 
    m_materialLibraryRevision=materials.Revision();
}

void VulkanRenderer::DestroyMaterialTextures()
{
    for (auto& sets : m_materialDescriptorSets)
        if (!sets.empty() && m_descriptorPool != VK_NULL_HANDLE)
            vkFreeDescriptorSets(m_device, m_descriptorPool, static_cast<uint32_t>(sets.size()), sets.data());
    m_materialDescriptorSets.clear();
    for (GpuTexture& texture : m_materialTextures)
        DestroyImage(texture.image, texture.view, texture.allocation, texture.sampler);
    m_materialTextures.clear();
}

void VulkanRenderer::ReloadTexture()
{
    // 可能失败的情况是文件写了一半被捕获，此时继续显示已经可用的纹理，让程序继续进行
    Image image;
    try {
        image = LoadImage(TextureAssetPath());
    }
    catch (const std::exception& exception) {
        std::cerr << "[hotreload] " << exception.what() << std::endl;
        std::cerr << "[hotreload] keeping the previously loaded texture" << std::endl;
        return;
    }

    // 在旧图像被销毁时，不能读取它，而且描述符会设置其视图。
    // vkDeviceWaitIdle 比帧循环使用的栅栏等待更重，而且这里可能会发生一次重新加载，保守一些不会带来可测量的成本。
    WaitIdle();

    DestroyTextureResources();
    UploadTexture(image);

    // 描述符集仍然持有旧的图像视图：关于重新加载的其他一切都是纹理本地的，而这个不是。
    // 忘记它会在下一帧产生验证错误或崩溃，而不会像看起来陈旧的纹理那么温和
    WriteTextureDescriptors();
    // 默认颜色图被无 baseColorTexture 的材质引用，下一帧重建它们的 binding。
    m_materialLibraryRevision = UINT64_MAX;

    std::cout << "[hotreload] texture reloaded: " << m_textureWidth << "x" << m_textureHeight
        << ", " << m_textureMipLevels << " mip levels" << std::endl;
}

void VulkanRenderer::CreateTextureSampler(uint32_t mipLevels)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
}

void VulkanRenderer::EnsureMeshesUploaded(const MeshLibrary& meshes)
{
    if (m_meshLibraryIdentity != meshes.Identity() || m_meshLibraryRevision != meshes.Revision()) {
        WaitIdle();
        for (GpuMesh& mesh : m_meshes) {
            DestroyBuffer(mesh.vertexBuffer, mesh.vertexAllocation);
            DestroyBuffer(mesh.indexBuffer, mesh.indexAllocation);
        }
        m_meshes.clear();
        m_meshLibraryIdentity = meshes.Identity();
        m_meshLibraryRevision = meshes.Revision();
    }
    const uint32_t target = meshes.Count();
    if (m_meshes.size() >= target) {
        return;
    }

    const uint32_t firstNew = static_cast<uint32_t>(m_meshes.size());
    m_meshes.resize(target);

    for (uint32_t index = firstNew; index < target; ++index) {
        UploadMesh(meshes.Get(index), index);
    }
}

void VulkanRenderer::UploadMesh(const MeshData& mesh, uint32_t meshIndex)
{
    GpuMesh& gpuMesh = m_meshes[meshIndex];

    UploadToDeviceLocal(mesh.vertices.data(),
        sizeof(Vertex) * mesh.vertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        gpuMesh.vertexBuffer, gpuMesh.vertexAllocation);

    UploadToDeviceLocal(mesh.indices.data(),
        sizeof(uint16_t) * mesh.indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        gpuMesh.indexBuffer, gpuMesh.indexAllocation);

    gpuMesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
}

void VulkanRenderer::UpdateUniformBuffer(uint32_t frameIndex, const RenderSnapshot& snapshot)
{
    UniformBufferObject ubo{};
    ubo.view = snapshot.camera.ViewMatrix();
    const float aspect = static_cast<float>(std::max(1u, m_viewportExtent.width)) /
        static_cast<float>(std::max(1u, m_viewportExtent.height));
    ubo.proj = snapshot.camera.ProjectionMatrix(aspect);
    ubo.proj[1][1] *= -1.0f;
    ubo.cameraPosition = glm::vec4(snapshot.camera.position, 1.0f);
    ubo.lightDirection = glm::vec4(snapshot.lightDirection, 0.0f);
    ubo.lightColor = glm::vec4(snapshot.lightColor, 1.0f);
    ubo.lightViewProjection = snapshot.lightViewProjection;
    ubo.inverseViewProjection = glm::inverse(ubo.proj * ubo.view);
    ubo.environment = glm::vec4(snapshot.environment.intensity,
        snapshot.environmentData && snapshot.environment.enabled ? 1.0f : 0.0f,
        snapshot.environmentData && snapshot.environment.background ? 1.0f : 0.0f,
        float(m_uploadedEnvironment->prefiltered.mips.size() - 1));
    ubo.lightingTerms = glm::vec4(snapshot.lightingTerms, 0.0f);
    ubo.framebufferSize = glm::vec4(std::max(1u, m_viewportExtent.width), std::max(1u, m_viewportExtent.height), 0, 0);
    memcpy(m_uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    // 和暂存上传一样的道理：一致性不再是分配请求所保证的，所以每次主机写入之后都会紧跟一次显式刷新。
    // 在一致性内存上这是无操作的，而在非一致性内存上，如果省略它，GPU 就会渲染缓冲区之前存储的内容。
    vmaFlushAllocation(m_allocator, m_uniformBufferAllocations[frameIndex], 0, sizeof(ubo));
}

void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
    const RenderSnapshot& snapshot) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    if (m_timestampPool) vkCmdResetQueryPool(commandBuffer, m_timestampPool, m_currentFrame * 5, 5);
    Timestamp(commandBuffer, 0);
    if (m_viewportImage.Valid()) {
        RecordShadowPass(commandBuffer, snapshot);
        Timestamp(commandBuffer, 1);

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_renderPass;
        renderPassInfo.framebuffer = m_viewportTargets[m_currentFrame].framebuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = VkExtent2D{m_viewportExtent.width, m_viewportExtent.height};

        std::array<VkClearValue, 3> clearValues{};
        clearValues[0].color = { {0.05f, 0.06f, 0.09f, 1.0f} };
        clearValues[1].color = { {0.05f, 0.06f, 0.09f, 0.0f} };
        clearValues[2].depthStencil = { 1.0f, 0 };
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());;
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_viewportExtent.width);
        viewport.height = static_cast<float>(m_viewportExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = VkExtent2D{m_viewportExtent.width, m_viewportExtent.height};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
            1, 1, &m_environmentSets[m_currentFrame], 0, nullptr);
        for (const DrawItem& item : snapshot.drawItems) {
            assert(item.meshIndex < m_meshes.size() && "draw item references a mesh that was never uploaded");

            const GpuMesh& mesh = m_meshes[item.meshIndex];
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                item.mirrored ? m_mirroredPipeline : m_graphicsPipeline);
            VkDescriptorSet descriptorSet = m_materialDescriptorSets.at(item.materialIndex)[m_currentFrame];
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            const VkBuffer vertexBuffers[] = { mesh.vertexBuffer };
            const VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            const PushConstants push{ item.worldTransform, glm::vec4(item.tint, 1.0f),
                glm::vec4(item.metallicFactor, item.roughnessFactor, item.unlit ? 1.0f : 0.0f, item.normalScale), item.emissive,
                glm::vec4(snapshot.normalMapping && item.normalTextureIndex != UINT32_MAX ? 1.0f : 0.0f,
                    float(snapshot.normalDebug), 0.0f, 0.0f) };
            vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PushConstants), &push);

            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
            if (m_configuration.transport) { ++m_metrics.sceneDraws; m_metrics.sceneTriangles += mesh.indexCount / 3; }
        }

        Timestamp(commandBuffer, 2);
        vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_toneMapPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_toneMapPipelineLayout, 0, 1, &m_viewportTargets[m_currentFrame].toneSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_toneMapPipelineLayout,
            1, 1, &m_environmentSets[m_currentFrame], 0, nullptr);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRenderPass(commandBuffer);
    } // LDR 最终为 SHADER_READ_ONLY；离屏 pass 的 external dependency 保障 UI 采样。
    Timestamp(commandBuffer, 3);

    VkRenderPassBeginInfo uiPass{};
    uiPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    uiPass.renderPass = m_uiRenderPass;
    uiPass.framebuffer = m_swapChainFramebuffers[imageIndex];
    uiPass.renderArea.extent = m_swapChainExtent;
    VkClearValue uiClear{}; 
    uiClear.color = {{0.025f, 0.025f, 0.03f, 1.0f}};
    uiPass.clearValueCount = 1; 
    uiPass.pClearValues = &uiClear;
    vkCmdBeginRenderPass(commandBuffer, &uiPass, VK_SUBPASS_CONTENTS_INLINE);
    // UI 在 tone-map 合成之后绘制，保持 UI 颜色不受场景曝光曲线影响。
    if (m_imguiInitialized) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

    vkCmdEndRenderPass(commandBuffer);
    Timestamp(commandBuffer, 4);
    if (m_timestampPool) m_timestampFrames[m_currentFrame] = m_metrics.frame;
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

// ---------------------------------------------------------------------------
// Dear ImGui overlay
// ---------------------------------------------------------------------------

void VulkanRenderer::InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_imguiContextCreated = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    // EditorLayout 通过内存接口加载/保存，避免隐式读取工作目录中的旧 imgui.ini。
    io.IniFilename = nullptr;
    // Image 不抢 ActiveId；窗口只能从标题栏移动，避免场景点击/操纵轴带动整个面板。
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(m_window, true))
        throw std::runtime_error("failed to initialize ImGui GLFW backend");
    m_imguiGlfwInitialized = true;

    // ImGui 需要一个descriptor pool来存放它的字体图集，且在交换链重建时依然存在。
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 100;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ImGui descriptor pool!");
    }

    InitImGuiVulkanBackend();
}

void VulkanRenderer::InitImGuiVulkanBackend()
{
    const QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = m_instance;
    initInfo.PhysicalDevice = m_physicalDevice;
    initInfo.Device = m_device;
    initInfo.QueueFamily = indices.graphicsFamily.value();
    initInfo.Queue = m_graphicsQueue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = m_imguiDescriptorPool;
    initInfo.RenderPass = m_uiRenderPass;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = m_minImageCount;
    initInfo.ImageCount = static_cast<uint32_t>(m_swapChainImages.size());
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;
    initInfo.UseDynamicRendering = false;
    // 屏蔽关于小型分配的“最佳实践”验证消息。
    initInfo.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("failed to initialize the ImGui Vulkan backend!");
    }

    m_imguiInitialized = true;

    // 自从 1.91 版本起，后端会自己上传字体图集，所以不再有 command-buffer 参数，
    // 而且 DestroyFontUploadObjects() 已被 DestroyFontsTexture() 替代（Shutdown() 会处理）。
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        throw std::runtime_error("failed to create the ImGui font texture!");
    }
}

void VulkanRenderer::ShutdownImGui()
{
    UnregisterViewportTextures();
    if (m_imguiInitialized) {
        ImGui_ImplVulkan_Shutdown();
        m_imguiInitialized = false;
    }

    if (m_imguiGlfwInitialized) {
        ImGui_ImplGlfw_Shutdown();
        m_imguiGlfwInitialized = false;
    }
    if (m_imguiContextCreated) {
        ImGui::DestroyContext();
        m_imguiContextCreated = false;
    }

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::BuildImGuiFrame(const std::function<void()>& drawEditorUi)
{
    // 共享编辑器先提交 DockSpace；诊断面板随后参与同一布局。
    drawEditorUi();
    if (m_configuration.transport) return;

    ImGui::Begin("Renderer Diagnostics", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted("Vulkan");

    // io.Framerate 在第一帧为 0
    const ImGuiIO& io = ImGui::GetIO();
    if (io.Framerate > 0.0f) {
        ImGui::Text("Frame: %.2f ms  (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    }
    else {
        ImGui::TextUnformatted("Frame: -- (warming up)");
    }

    ImGui::Separator();
    ImGui::Text("GPU: %s", m_deviceName.c_str());
    ImGui::Text("Swapchain: %u x %u", m_swapChainExtent.width, m_swapChainExtent.height);
    ImGui::Text("Images: %zu  (min %u)", m_swapChainImages.size(), m_minImageCount);
    ImGui::Text("Frames in flight: %u", kMaxFramesInFlight);
    ImGui::Separator();
    ImGui::End();

}

VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    // SPIR-V 是一串的 32 位字；vector<char> 只是一个字节视图。
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

VkFormat VulkanRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features) const
{
    for (const VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find a supported format!");
}

VkFormat VulkanRenderer::FindDepthFormat() const
{
    // 首选D32 first, 然后是 depth/stencil 混合格式. 因为不能没有一个能保证在所有驱动都能用，所有保留候选名单
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void VulkanRenderer::CreateRenderPass()
{
    std::array<VkAttachmentDescription, 3> attachments{};
    attachments[0].format = m_ldrFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1].format = m_hdrFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[2].format = FindDepthFormat();
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference sceneColorRef{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference sceneDepthRef{ 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentReference ldrColorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference hdrInputRef{ 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    std::array<VkSubpassDescription, 2> subpasses{};
    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = 1;
    subpasses[0].pColorAttachments = &sceneColorRef;
    subpasses[0].pDepthStencilAttachment = &sceneDepthRef;
    subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 1;
    subpasses[1].pColorAttachments = &ldrColorRef;
    subpasses[1].inputAttachmentCount = 1;
    subpasses[1].pInputAttachments = &hdrInputRef;

    std::array<VkSubpassDependency, 4> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = 1;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].srcSubpass = 1;
    dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    // LDR 在 subpass 1 写入；复用前覆盖上次 UI 对此槽位的采样。
    dependencies[3].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[3].dstSubpass = 1;
    dependencies[3].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[3].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[3].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[3].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void VulkanRenderer::CreateUiRenderPass()
{
    VkAttachmentDescription color{};
    color.format = m_swapChainImageFormat; color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
    dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1; info.pAttachments = &color;
    info.subpassCount = 1; info.pSubpasses = &subpass;
    info.dependencyCount = 1; info.pDependencies = &dependency;
    if (vkCreateRenderPass(m_device, &info, nullptr, &m_uiRenderPass) != VK_SUCCESS)
        throw std::runtime_error("failed to create editor UI render pass");
}

void VulkanRenderer::UploadToDeviceLocal(const void* source, VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& buffer, VmaAllocation& allocation)
{
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    VmaAllocationInfo stagingInfo{};

    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::HostVisible, stagingBuffer, stagingAllocation, &stagingInfo);
    ScopeExit stagingGuard([&] { DestroyBuffer(stagingBuffer, stagingAllocation); });

    std::memcpy(stagingInfo.pMappedData, source, static_cast<size_t>(size));

    vmaFlushAllocation(m_allocator, stagingAllocation, 0, size);

    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, MemoryUse::DeviceLocal, buffer, allocation);

    CopyBuffer(stagingBuffer, buffer, size);

    // 必须走 DestroyBuffer：这块 staging buffer 的内存归 VMA 管，
    // 直接 vkDestroyBuffer 只会销毁 buffer 对象，VMA 那边仍以为 allocation 活着 ——
    // 于是 vmaDestroyAllocator 时会断言 "Some allocations were not freed"。
    DestroyBuffer(stagingBuffer, stagingAllocation);
}

void VulkanRenderer::CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkImage& image, VmaAllocation& allocation, uint32_t arrayLayers, VkImageCreateFlags flags)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.flags = flags;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // 这个引擎创建的图像都没有主机可见的用途。纹理是通过暂存缓冲区传入的，而深度缓冲区根本不会离开 GPU。
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr)
        != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }
}

VkImageView VulkanRenderer::CreateImageView(VkImage image, VkFormat format,
    VkImageAspectFlags aspectFlags, uint32_t mipLevels, bool cube) const
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    // view必须暴露整个mip链，否则采样器只能读取第0级，其余mip链对它来说是看不见的。
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = cube ? 6 : 1;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return imageView;
}

void VulkanRenderer::TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t baseMipLevel, uint32_t levelCount, uint32_t layerCount) const
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    // VK_QUEUE_FAMILY_IGNORED 因为这里图形队列和呈现队列属于同一个队列族，所以不涉及队列所有权的转移。
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

    // stage 和 access masks 必须描述这个屏障两侧的实际生成者和使用者。
    // 把一个有效的配对复制到错误的转换中，就会出现只有在负载下才会显现的竞争条件，
    // 所以每个支持的转换都被明确列出，其他任何情况都会被明令拒绝，而不是默默地导致不同步。
    VkPipelineStageFlags sourceStage = 0;
    VkPipelineStageFlags destinationStage = 0;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        throw std::runtime_error("unsupported image layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void VulkanRenderer::CopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = {
        width,
        height,
        1
    };
    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
}

void VulkanRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, MemoryUse memoryUse, 
                                  VkBuffer& buffer, VmaAllocation& allocation, VmaAllocationInfo* allocationInfo) const {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    switch (memoryUse) {
    case MemoryUse::DeviceLocal:
        // 更倾向于使用设备而不是严格要求仅使用 GPU。VMA允许在设备本地堆无法满足请求时回退到主机内存，
        // 而且一个仍然可用的回退方案总比直接失败要好——这是人工编写的内存类型搜索根本无法表达的权衡。
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;

    case MemoryUse::HostVisible:
        // 同时映射和主机访问，指针来自 VmaAllocationInfo::pMappedData。
        // 这绕过了一个问题，即映射调用返回的是分配的起始地址还是缓冲区的起始地址：pMappedData 明确就是缓冲区。
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }
    // 在一次调用中分配并绑定，同时检查每个中间结果。
    // 它替代之前的四个调用：vkCreateBuffer → vkGetBufferMemoryRequirements → vkAllocateMemory → vkBindBufferMemory，
    // vkBindBufferMemory的结果一般没人检查，绑定失败会在后面表现为垃圾几何体，而不是报错。
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, allocationInfo)
        != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }
}

void VulkanRenderer::CreateAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    // 对应 Instance 的版本，VMA 使用 Vulkan 1.2的入口点（vkBindBufferMemory2、vkGetBufferMemoryRequirements2、...）
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("failed to create the Vulkan memory allocator!");
    }
}

void VulkanRenderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    EndSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::DestroyBuffer(VkBuffer& buffer, VmaAllocation& allocation)
{
    // 空指针是安全的
    if (buffer != VK_NULL_HANDLE || allocation != nullptr)
        vmaDestroyBuffer(m_allocator, buffer, allocation);
    buffer = VK_NULL_HANDLE;
    allocation = nullptr;
}

void VulkanRenderer::DestroyImage(VkImage& image, VkImageView& imageView, VmaAllocation& allocation, VkSampler& imageSampler)
{
    // 用参数，不要用成员：这个 helper 必须能销毁调用者给的那一份资源
    // （写成 m_textureImageView 时它只对贴图"碰巧"有效，换成深度图就会销毁错的 view）。
    // 顺序也不能换：view / sampler 都在 image 之前。
    if (imageSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, imageSampler, nullptr);
        imageSampler = VK_NULL_HANDLE;
    }
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE || allocation != nullptr)
        vmaDestroyImage(m_allocator, image, allocation);
    image = VK_NULL_HANDLE;
    allocation = nullptr;
}

VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE; 
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanRenderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanRenderer::CreateTimingQueries()
{
    m_deviceInfo.backend = "vulkan"; m_deviceInfo.device = m_deviceName;
#ifndef NDEBUG
    m_deviceInfo.validation = true;
#endif
    if (!m_configuration.transport) return;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count, families.data());
    m_timestampBits = families.at(*FindQueueFamilies(m_physicalDevice).graphicsFamily).timestampValidBits;
    if (!m_timestampBits) return;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    m_timestampPeriod = properties.limits.timestampPeriod;
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP; info.queryCount = kMaxFramesInFlight * 5;
    if (vkCreateQueryPool(m_device, &info, nullptr, &m_timestampPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create GPU timestamp queries");
    m_deviceInfo.gpuTimestamps = true;
}
void VulkanRenderer::Timestamp(VkCommandBuffer buffer, uint32_t marker)
{
    if (m_timestampPool) vkCmdWriteTimestamp(buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        m_timestampPool, m_currentFrame * 5 + marker);
}
void VulkanRenderer::ReadTimingQueries()
{
    if (!m_timestampPool || !m_timestampFrames[m_currentFrame]) return;
    std::array<uint64_t,5> ticks{};
    // Read only after the normal frame-slot fence. No additional waits or WAIT_BIT.
    const auto result = vkGetQueryPoolResults(m_device, m_timestampPool, m_currentFrame * 5, 5,
        sizeof(ticks), ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result == VK_SUCCESS) {
        const uint64_t mask = m_timestampBits == 64 ? UINT64_MAX : (uint64_t(1) << m_timestampBits) - 1;
        const auto ms = [&](int a, int b) { return double((ticks[b] - ticks[a]) & mask) * m_timestampPeriod / 1e6; };
        m_gpuSamples.push_back({m_timestampFrames[m_currentFrame], ms(0,1), ms(1,2), ms(2,3), ms(3,4)});
    } else if (result == VK_NOT_READY) ++m_skippedGpuSamples;
    else throw std::runtime_error("Failed reading GPU timestamp queries");
    m_timestampFrames[m_currentFrame] = 0;
}
