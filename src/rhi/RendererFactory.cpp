#include "RendererFactory.h"
#include "Renderer.h"
#include "vulkan/vulkanRenderer.h"

std::unique_ptr<Renderer> CreateRenderer(GraphicAPIType type)
{
	if (type == GraphicAPIType::VULKAN)
	{
		std::cout << "[renderer] backend: Vulkan" << std::endl;
		return std::make_unique<VulkanRenderer>();
	}
	else
	{
		std::cout << "[renderer] backend: OpenGL" << std::endl;
		throw std::runtime_error("the OpenGL backend is not implemented yet");
	}
}
