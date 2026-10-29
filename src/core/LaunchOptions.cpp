#include "core/LaunchOptions.h"

#include <stdexcept>

const char* LaunchUsage()
{
    return "VulkanOpenGLEngine [vulkan|opengl] [--scene path.scene.json]\n"
        "VulkanOpenGLEngine [vulkan|opengl] --play path.scene.json\n"
        "VulkanOpenGLEngine [vulkan|opengl] --industrial path.scene.json (alias for --play)\n"
        "VulkanOpenGLEngine --prepare-industrial assets/industrial\n"
        "VulkanOpenGLEngine --help";
}

LaunchOptions ParseLaunchOptions(int argc, char** argv)
{
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "opengl" || arg == "vulkan") { options.backend = arg; continue; }
        if (arg == "--help") { options.help = true; continue; }
        if (arg != "--play" && arg != "--industrial" && arg != "--scene" && arg != "--prepare-industrial")
            throw std::invalid_argument("Unknown argument: " + arg + "\n" + LaunchUsage());
        if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + arg);
        const std::string value = argv[++i];
        const auto path = std::filesystem::path(std::u8string(value.begin(), value.end()));
        if (arg == "--industrial" || arg == "--play") options.playerScene = path;
        else if (arg == "--scene") options.editorScene = path;
        else options.prepareIndustrialPath = path;
    }
    const int modes = int(!options.playerScene.empty()) +
        int(!options.editorScene.empty()) + int(!options.prepareIndustrialPath.empty());
    if (modes > 1) 
        throw std::invalid_argument("Choose one launch mode");
    return options;
}
