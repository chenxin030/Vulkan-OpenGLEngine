#include "core/Application.h"
#include "editor/EditorLayer.h"
#include "rhi/Renderer.h"
#include "rhi/RendererFactory.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const std::string backendName = (argc > 1) ? argv[1] : std::string();
    try {
        const GraphicAPIType apiType =
            (backendName == "opengl") ? GraphicAPIType::OPENGL : GraphicAPIType::VULKAN;
        Application application(CreateRenderer(apiType));
        application.PushLayer(std::make_unique<EditorLayer>());
        application.InstallImGuiOverlay();
        application.Run();
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
