#include "editor/EditorLayout.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void CreateContext()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = ImVec2(1400, 860);
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
}
bool Frame(EditorLayout& layout)
{
    ImGui::NewFrame();
    const bool rebuilt = layout.DrawDockSpace();
    for (const char* name : {"Scene Hierarchy", "Scene View", "Inspector", "Assets", "Renderer Diagnostics"}) {
        if (ImGui::Begin(name)) ImGui::TextUnformatted(name);
        ImGui::End();
    }
    ImGui::Render();
    layout.SaveIfRequested();
    return rebuilt;
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main()
{
    const auto directory = std::filesystem::path(u8"" LAYOUT_TEST_DIR);
    std::filesystem::create_directories(directory);
    const auto path = directory / u8"工作区.ini";
    std::filesystem::remove(path);
    try {
        CreateContext();
        EditorLayout first; first.Initialize(path);
        Check(Frame(first), "missing file must build default");
        Frame(first); Frame(first);
        Check(!Frame(first), "ordinary frames must preserve layout");
        const auto sceneDock = ImGui::FindWindowByName("Scene View")->DockId;
        Check(sceneDock != 0, "scene must dock");
        Check(ImGui::FindWindowByName("Assets")->DockId == ImGui::FindWindowByName("Renderer Diagnostics")->DockId,
            "assets and diagnostics must share tabs");
        Check(ImGui::FindWindowByName("Inspector")->DockId != sceneDock, "inspector must have independent area");
        Check(!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable), "one native window only");
        ImGui::DockBuilderDockWindow("Inspector", sceneDock);
        Frame(first); Frame(first); first.Save();
        const std::string custom = Read(path);
        Check(custom.find("[Docking][Data]") != std::string::npos, "save actual docking settings");
        ImGui::DestroyContext();

        CreateContext();
        EditorLayout restored; restored.Initialize(path);
        Check(!Frame(restored), "loaded workspace must not rebuild");
        Frame(restored);
        Check(ImGui::FindWindowByName("Inspector")->DockId == ImGui::FindWindowByName("Scene View")->DockId,
            "custom docking must survive context recreation");
        restored.RequestReset();
        Check(Frame(restored), "reset must rebuild during next UI frame");
        Frame(restored);
        Check(ImGui::FindWindowByName("Inspector")->DockId != ImGui::FindWindowByName("Scene View")->DockId,
            "reset must restore default docking");
        restored.Save();
        Check(Read(path) != custom, "reset must persist new layout");
        const auto original = Read(path);
        const auto temporary = std::filesystem::path(path).concat(".tmp");
        std::filesystem::create_directory(temporary);
        { std::ofstream block(temporary / "blocker.txt"); block << "block replacement"; }
        restored.Save();
        Check(restored.Status().find("failed") != std::string::npos, "write failure must be reported");
        Check(Read(path) == original, "failed save must preserve original file");
        std::filesystem::remove(temporary / "blocker.txt"); std::filesystem::remove(temporary);
        restored.Save();
        Check(restored.Status() == "Layout saved", "manual retry must work");
        ImGui::DestroyContext();

        { std::ofstream bad(path, std::ios::trunc); bad << "invalid layout without workspace"; }
        CreateContext();
        EditorLayout invalid; invalid.Initialize(path);
        Check(Frame(invalid), "missing dock root must recover default");
        Frame(invalid);
        invalid.Save();
        ImGui::DestroyContext();
        CreateContext();
        EditorLayout recovered; recovered.Initialize(path);
        Check(!Frame(recovered), "recovered layout must survive restart");
        ImGui::DestroyContext();
        std::cout << "Editor layout checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        return 1;
    }
}
