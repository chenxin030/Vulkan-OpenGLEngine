#include "platform/FileDialogs.h"
#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

namespace {
std::optional<std::filesystem::path> Pick(const std::filesystem::path& directory,
    const wchar_t* filter, const wchar_t* title, bool save, const std::filesystem::path& filename = {})
{
    std::vector<wchar_t> file(32768, L'\0');
    const auto suggested = filename.wstring();
    if (suggested.size() >= file.size()) throw std::runtime_error("Suggested filename is too long");
    std::copy(suggested.begin(), suggested.end(), file.begin());
    OPENFILENAMEW options{};
    options.lStructSize = sizeof(options);
    options.hwndOwner = GetActiveWindow();
    options.lpstrFilter = filter;
    options.lpstrFile = file.data();
    options.nMaxFile = static_cast<DWORD>(file.size());
    options.lpstrInitialDir = directory.c_str();
    options.lpstrTitle = title;
    options.lpstrDefExt = save ? L"scene.json" : nullptr;
    options.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&options) : GetOpenFileNameW(&options)) return std::filesystem::path(file.data());
    if (const DWORD error = CommDlgExtendedError(); error != 0)
        throw std::runtime_error("File dialog failed: " + std::to_string(error));
    return std::nullopt;
}
}
#endif

std::optional<std::filesystem::path> FileDialogs::OpenModel(const std::filesystem::path& directory)
{
#ifdef _WIN32
    return Pick(directory, L"glTF model (*.gltf;*.glb)\0*.gltf;*.glb\0\0", L"Import Model", false);
#else
    throw std::runtime_error("Native file dialogs are currently implemented for Windows");
#endif
}
std::optional<std::filesystem::path> FileDialogs::OpenScene(const std::filesystem::path& directory)
{
#ifdef _WIN32
    return Pick(directory, L"Scene (*.scene.json)\0*.scene.json\0\0", L"Open Scene", false);
#else
    throw std::runtime_error("Native file dialogs are currently implemented for Windows");
#endif
}
std::optional<std::filesystem::path> FileDialogs::SaveScene(const std::filesystem::path& directory,
    const std::filesystem::path& filename)
{
#ifdef _WIN32
    return Pick(directory, L"Scene (*.scene.json)\0*.scene.json\0\0", L"Save Scene As", true, filename);
#else
    throw std::runtime_error("Native file dialogs are currently implemented for Windows");
#endif
}
