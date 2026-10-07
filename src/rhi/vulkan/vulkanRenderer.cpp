#include "vulkanRenderer.h"

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
#include <scene/Image.h>
#include <scene/Scene.h>

std::string TextureAssetPath()
{
    return std::string(ASSET_DIR) + "/checkerboard.ppm";
}

namespace {

// 纹理格式只在这里定义一次：上传、采样器、以及"能不能 blit"的能力查询必须问同一个格式。
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_SRGB;

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

std::array<VkVertexInputAttributeDescription, 3> VertexAttributeDescriptions()
{
    // 故意逐字段分配：offsetof 返回 size_t，把它转换为花括号初始化器里的 uint32_t `offset` 成员算是窄化转换，有些编译器会直接拒绝。
    std::array<VkVertexInputAttributeDescription, 3> attributes{};

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

    CreateAllocator();

    CreateSwapChain();
    CreateImageViews();
    CreateDepthResources();

    CreateCommandPool();
    CreateCommandBuffers();

    CreateTextureImage();

    CreateUniformBuffers();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateRenderPass();
    CreateGraphicsPipeline();
    CreateFramebuffers();

    CreateSyncObjects();

    InitImGui();
}

void VulkanRenderer::Cleanup() {
    WaitIdle();

    ShutdownImGui();

    if (!m_imageAvailableSemaphores.empty()) {
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
        }
        m_imageAvailableSemaphores.resize(0);
        m_inFlightFences.resize(0);
    }
    // m_renderFinishedSemaphores 属于交换链，会在下面的 CleanupSwapChain() 中释放。

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    CleanupSwapChain();

    DestroyTextureResources();

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
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
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
void VulkanRenderer::DrawFrame(const Scene& scene) {

    // 轮询帧缓冲区大小而不是使用调整大小回调可以减少动用的部分，而且还能自动处理 DPI 变化。
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);

    if (width == 0 || height == 0) {
        // 防止最小化后 spin.
        glfwWaitEvents();
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

    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE, &imageIndex);

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

    UpdateUniformBuffer(m_currentFrame, scene);

    // 在录制之前先构建 UI，这样命令缓冲区就会使用这一帧的绘制数据。
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    BuildImGuiFrame();
    ImGui::Render();

    EnsureMeshesUploaded(scene.meshes);

    RecordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, scene);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    const VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    const VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    // 按 imageIndex 索引，而不是按 m_currentFrame —— 原因见 CreateRenderFinishedSemaphores()。
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

    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

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
    // 最小化的窗口会显示0x0；阻塞直到它再次有真实大小。
    int width = 0;
    int height = 0;
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    WaitIdle();

    const VkFormat previousFormat = m_swapChainImageFormat;

    // 这里只销毁与交换链大小相关的资源（包括 renderFinished 信号量）。
    // 当格式不变时，渲染通道、管线会重复使用。
    DestroySwapChainResources();

    CreateSwapChain();
    CreateImageViews();
    CreateDepthResources();

    if (m_swapChainImageFormat != previousFormat) {
        // 顺序很重要：ImGui 的 Vulkan 后端会把渲染通道烘焙到它自己的管线中，
        // 所以必须在渲染通道被销毁之前关闭它。
        const bool hadImGuiBackend = m_imguiInitialized;
        if (hadImGuiBackend) {
            ImGui_ImplVulkan_Shutdown();
            m_imguiInitialized = false;
        }

        // 表面格式发生了变化（很少见），让渲染通道和所有以此为基础构建的管线都过时了。
        DestroyPipelineResources();
        CreateRenderPass();
        CreateGraphicsPipeline();

        if (hadImGuiBackend) {
            InitImGuiVulkanBackend();
        }
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
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // 采样只发生在片元着色器里。stageFlags 留空（= 0）是个"沉默"的错：
    // 只要没有任何 shader 真的碰 binding 1，它就一直不报错 —— 一旦片元采样
    // 打开，vkCreateGraphicsPipelines 立刻判违规（VUID-...-layout-07988）。
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
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
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(kMaxFramesInFlight);

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
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

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

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
    // Mailbox 最接近“无上限但不丢失”. FIFO 是唯一保证存在的模式，所以它是备用方案。
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

void VulkanRenderer::CreateDepthResources() {
    VkFormat depthFormat = FindDepthFormat();
    CreateImage(m_swapChainExtent.width, m_swapChainExtent.height, 1, depthFormat, VK_IMAGE_TILING_OPTIMAL, 
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_depthImage, m_depthImageAllocation);
    m_depthImageView = CreateImageView(m_depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

void VulkanRenderer::CreateGraphicsPipeline()
{
    const std::vector<char> vertShaderCode = ReadFile(std::string(SHADER_DIR) + "/mesh.vert.spv");
    const std::vector<char> fragShaderCode = ReadFile(std::string(SHADER_DIR) + "/mesh.frag.spv");

    const VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
    const VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

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
    const std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = VertexAttributeDescriptions();

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
    const VkDescriptorSetLayout setLayouts[] = { m_descriptorSetLayout };

    // model矩阵作为 push 常量传入。
    // 阶段标志必须和着色器实际读取它的地方匹配——这里只在顶点阶段。
    // 搞错了的话不会在管线创建时失败，而会在绘制时失败。
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
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

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
}

void VulkanRenderer::CreateFramebuffers()
{
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i) {
        std::array<VkImageView, 2> attachments = {
            m_swapChainImageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
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
// 故意拆分成两个拆解步骤：仅调整大小会使交换链大小的资源失效，而格式更改则会使渲染通道和所有基于它构建的东西（包括ImGui后端）都失效。
// ---------------------------------------------------------------------------

void VulkanRenderer::DestroySwapChainResources()
{
    DestroyRenderFinishedSemaphores();

    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    vmaDestroyImage(m_allocator, m_depthImage, m_depthImageAllocation);
    m_depthImage = VK_NULL_HANDLE;
    m_depthImageAllocation = nullptr;

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
    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
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
    m_textureWidth = image.width;
    m_textureHeight = image.height;

    // 完整 mip 链直到 1x1：最长边的 log2，加上基础层。
    m_textureMipLevels = static_cast<uint32_t>(std::floor(
        std::log2(static_cast<double>(std::max(image.width, image.height))))) + 1;

    // 规范并不保证 mip 级别之间的线性 blit：一个格式能采样，却可能拒绝当 blit 的源或目标。
    // 先问，而不是在某些厂商驱动上发布一张全黑的贴图。
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, kTextureFormat, &formatProperties);

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
        m_physicalDevice, kTextureFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
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

    std::memcpy(stagingInfo.pMappedData, image.pixels.data(), static_cast<size_t>(imageSize));
    vmaFlushAllocation(m_allocator, stagingAllocation, 0, imageSize);

    // SRGB 而不是 UNORM：采样器在读取时把 sRGB 转成线性，交换链在写出时再转回去。
    // 这一对弄错就是"贴图发白/发灰"的经典原因。
    //
    // TRANSFER_SRC 和 TRANSFER_DST 都要：mip 链是在同一张 image 内部逐层 blit 出来的。
    CreateImage(image.width, image.height, m_textureMipLevels, kTextureFormat,
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
    m_textureImageView = CreateImageView(m_textureImage, kTextureFormat,
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
    // 首次使用时懒加载，而不是在初始化时急切加载，因为初始化根本不知道世界里有什么
    // 而且一个必须先拿到网格列表才能开始的渲染器，就是一个坚持要提前知道整个世界的渲染器。
    // 现在，大小比较就是完整的过时性检查，这在网格不可变且只能追加时已经足够。热重载会把它变成每条条目的版本检查，而这个改动只会在这个函数内部进行。
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

void VulkanRenderer::UpdateUniformBuffer(uint32_t frameIndex, const Scene& scene)
{
    UniformBufferObject ubo{};
    ubo.view = scene.camera.ViewMatrix();
    const float aspect = static_cast<float>(m_swapChainExtent.width) /
        static_cast<float>(m_swapChainExtent.height);
    ubo.proj = scene.camera.ProjectionMatrix(aspect);
    ubo.proj[1][1] *= -1.0f;
    memcpy(m_uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    // 和暂存上传一样的道理：一致性不再是分配请求所保证的，所以每次主机写入之后都会紧跟一次显式刷新。
    // 在一致性内存上这是无操作的，而在非一致性内存上，如果省略它，GPU 就会渲染缓冲区之前存储的内容。
    vmaFlushAllocation(m_allocator, m_uniformBufferAllocations[frameIndex], 0, sizeof(ubo));
}

void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Scene& scene) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    // 和 OpenGL 后端用**同一个背景色**（那边是 glClearColor）。两个后端画同一个场景，
    // 所以"背景算不算画面的一部分"这个问题现在有答案了：算。
    // 这个值目前**没有单一归属**（两边各写一份，改一处就要改另一处）—— 要不要把它提到
    // Renderer 接口上，是 `07`"下一步"练习 3 那个问题（SetClearColor 该不该长出来）。
    clearValues[0].color = { {0.05f, 0.06f, 0.09f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());;
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapChainExtent.width);
    viewport.height = static_cast<float>(m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);

    for (const DrawItem& item : scene.drawItems) {
        // meshIndex 是 MeshLibrary 的合法索引 —— 由构建场景那一侧用 k* 常量保证。
        // 这里是坏句柄最终落地的地方：用 assert 而不是分支，debug 抓住、release 零成本。
        assert(item.meshIndex < m_meshes.size() && "draw item references a mesh that was never uploaded");

        const GpuMesh& mesh = m_meshes[item.meshIndex];

        const VkBuffer vertexBuffers[] = { mesh.vertexBuffer };
        const VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

        const PushConstants push{ item.worldTransform };
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(PushConstants), &push);

        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    }

    // ImGui 会把它的绘制命令记录到同一个 renderpass 和 subpass 里;
    // 这就是为什么后端与渲染过程对象绑定。
    if (m_imguiInitialized) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

    vkCmdEndRenderPass(commandBuffer);
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

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

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
    initInfo.RenderPass = m_renderPass;
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
    if (m_imguiInitialized) {
        ImGui_ImplVulkan_Shutdown();
        m_imguiInitialized = false;
    }

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::BuildImGuiFrame()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);

    ImGui::Begin("Mini Engine - Phase 0");

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
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = FindDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // 在操作任意附件之前，先等交换链图像可用，并且上一帧的深度写入可见。
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void VulkanRenderer::UploadToDeviceLocal(const void* source, VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& buffer, VmaAllocation& allocation)
{
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    VmaAllocationInfo stagingInfo{};

    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::HostVisible, stagingBuffer, stagingAllocation, &stagingInfo);

    std::memcpy(stagingInfo.pMappedData, source, static_cast<size_t>(size));

    vmaFlushAllocation(m_allocator, stagingAllocation, 0, size);

    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, MemoryUse::DeviceLocal, buffer, allocation);

    CopyBuffer(stagingBuffer, buffer, size);

    // 必须走 DestroyBuffer：这块 staging buffer 的内存归 VMA 管，
    // 直接 vkDestroyBuffer 只会销毁 buffer 对象，VMA 那边仍以为 allocation 活着 ——
    // 于是 vmaDestroyAllocator 时会断言 "Some allocations were not freed"。
    DestroyBuffer(stagingBuffer, stagingAllocation);
}

void VulkanRenderer::CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkImage& image, VmaAllocation& allocation)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
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
    VkImageAspectFlags aspectFlags, uint32_t mipLevels) const
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    // view必须暴露整个mip链，否则采样器只能读取第0级，其余mip链对它来说是看不见的。
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return imageView;
}

void VulkanRenderer::TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t baseMipLevel, uint32_t levelCount) const
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
    barrier.subresourceRange.layerCount = 1;

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
