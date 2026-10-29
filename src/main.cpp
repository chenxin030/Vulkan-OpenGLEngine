#include "core/Application.h"
#include "editor/EditorLayer.h"
#include "rhi/Renderer.h"
#include "rhi/RendererFactory.h"
#include "player/PlayerLayer.h"
#include "industrial/IndustrialScene.h"
#include "core/LaunchOptions.h"
#include "scene/SceneSerializer.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        const auto options = ParseLaunchOptions(argc, argv);
        if (options.help) { std::cout << LaunchUsage() << std::endl; return EXIT_SUCCESS; }
        if (!options.prepareIndustrialPath.empty()) {
            SceneSerializer::Save(BuildIndustrialScene(options.prepareIndustrialPath), options.prepareIndustrialPath/"industrial.scene.json");
            std::cout << "Industrial scene prepared" << std::endl;
            return EXIT_SUCCESS;
        }
        const bool player = !options.playerScene.empty();
        const GraphicAPIType apiType = (options.backend == "opengl") ? GraphicAPIType::OPENGL : GraphicAPIType::VULKAN;
        auto renderer = CreateRenderer(apiType);
        renderer->ShowDiagnostics(!player);

        ApplicationSpecification spec;
        if (player) { 
            spec.width = 1280; 
            spec.height = 720; 
            spec.title = ProjectInfo::PlayerTitle;
            spec.pixelSizedWindow = true; 
        }
        
        Application application(std::move(renderer), spec);
        if (player)
            application.PushLayer(std::make_unique<PlayerLayer>(options.playerScene, options.backend));
        else 
            application.PushLayer(std::make_unique<EditorLayer>(options.editorScene));

        application.InstallImGuiOverlay();
        application.Run();

        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
