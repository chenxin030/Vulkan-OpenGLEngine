#pragma once

#include <filesystem>
#include <optional>

namespace FileDialogs {
std::optional<std::filesystem::path> OpenModel(const std::filesystem::path& initialDirectory);
std::optional<std::filesystem::path> OpenScene(const std::filesystem::path& initialDirectory);
std::optional<std::filesystem::path> SaveScene(const std::filesystem::path& initialDirectory,
    const std::filesystem::path& filename);
}
