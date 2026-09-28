#include <memory>
#include <iostream>

class Renderer;

enum GraphicAPIType {
	VULKAN,
	OPENGL,
};

std::unique_ptr<Renderer> CreateRenderer(GraphicAPIType type);
