#include "core/Application.h"
#include "editor/EditorLayer.h"
#include "rhi/Renderer.h"
#include "rhi/RendererFactory.h"
#include "transport/TransportLayer.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    try {
        const auto options = ParseTransportOptions(argc, argv);
        const GraphicAPIType apiType =
            (options.backend == "opengl") ? GraphicAPIType::OPENGL : GraphicAPIType::VULKAN;
        auto renderer = CreateRenderer(apiType);
        renderer->Configure({options.enabled, options.benchmark});
        auto* renderObserver = renderer.get();

        ApplicationSpecification spec;
        if (options.enabled) { spec.width = 1280; spec.height = 720; spec.title = "Transport benchmark"; spec.pixelSizedWindow = true; }
        
        Application application(std::move(renderer), spec);
        bool successful = !options.benchmark;
        if (options.enabled) application.PushLayer(std::make_unique<TransportLayer>(*renderObserver, options,
            [&](bool success) { successful = success; application.Close(); }));
        else application.PushLayer(std::make_unique<EditorLayer>());

        application.InstallImGuiOverlay();
        application.Run();

        return successful ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
