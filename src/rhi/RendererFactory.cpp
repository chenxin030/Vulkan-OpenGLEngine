#include "RendererFactory.h"
#include "Renderer.h"
#include "opengl/openglRenderer.h"
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
		return std::make_unique<OpenGLRenderer>();
	}
}
