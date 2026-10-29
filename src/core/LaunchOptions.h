#pragma once

#include <filesystem>
#include <string>

struct LaunchOptions
{
    bool help = false;
    std::string backend = "vulkan";
    std::filesystem::path playerScene, editorScene, prepareIndustrialPath;
};

LaunchOptions ParseLaunchOptions(int argc, char** argv);
const char* LaunchUsage();
