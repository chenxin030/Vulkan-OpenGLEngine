#include "editor/ProjectAssets.h"
#include "scene/ModelImporter.h"
#include "scene/Scene.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <stdexcept>
#include <system_error>
#include <string_view>

std::string AssetPathUtf8(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::filesystem::path AssetPathFromUtf8(const std::string& text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
namespace {
std::string Lower(std::string text)
{
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
}

bool IsAssetPathWithin(const std::filesystem::path& path, const std::filesystem::path& root)
{
    std::error_code error;
    const auto full = std::filesystem::weakly_canonical(path, error);
    if (error) return false;
    const auto base = std::filesystem::weakly_canonical(root, error);
    if (error || base.empty()) return false;
    const auto relative = full.lexically_relative(base);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

bool ProjectAssets::IsModel(const std::filesystem::path& path)
{
    const auto extension = Lower(AssetPathUtf8(path.extension()));
    return extension == ".gltf" || extension == ".glb";
}
bool ProjectAssets::IsScene(const std::filesystem::path& path)
{
    return Lower(AssetPathUtf8(path.filename())).ends_with(".scene.json");
}

void ProjectAssets::Initialize(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    m_root = std::filesystem::canonical(root);
    Refresh();
}
void ProjectAssets::Refresh()
{
    std::vector<ProjectAssetEntry> entries;
    for (std::filesystem::recursive_directory_iterator it(m_root, std::filesystem::directory_options::skip_permission_denied), end;
        it != end; ++it) {
        // 不跟随链接/临时导入目录；未发布的候选永远不出现在 Assets 中。
        const auto& file = *it;
        const bool privateCandidate = file.is_directory() && file.path().parent_path() == m_root / "models" &&
            AssetPathUtf8(file.path().filename()).starts_with(".import-");
        if (file.is_symlink() || privateCandidate) {
            if (file.is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (!IsAssetPathWithin(file.path(), m_root)) { it.disable_recursion_pending(); continue; }
        const auto relative = file.path().lexically_relative(m_root);
        if (file.is_directory()) entries.push_back({relative, ProjectAssetKind::Directory});
        else if (file.is_regular_file()) {
            if (IsModel(file.path())) entries.push_back({relative, ProjectAssetKind::Model});
            else if (IsScene(file.path())) entries.push_back({relative, ProjectAssetKind::Scene});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if ((a.kind == ProjectAssetKind::Directory) != (b.kind == ProjectAssetKind::Directory))
            return a.kind == ProjectAssetKind::Directory;
        return a.relativePath < b.relativePath;
    });
    m_entries = std::move(entries);
}
std::filesystem::path ProjectAssets::Resolve(const std::filesystem::path& path, ProjectAssetKind kind) const
{
    const auto full = std::filesystem::absolute(path.is_absolute() ? path : m_root / path).lexically_normal();
    if (!IsAssetPathWithin(full, m_root) || full == m_root)
        throw std::runtime_error("Choose a file inside the project assets directory");
    if ((kind == ProjectAssetKind::Scene && !IsScene(full)) ||
        (kind == ProjectAssetKind::Model && !IsModel(full)))
        throw std::runtime_error("Expected a .scene.json scene or a .gltf/.glb model");
    return full;
}
bool ProjectAssets::ValidImportName(const std::string& name)
{
    if (name.empty() || name.size() > 128 || name.front() == '.' || name.back() == '.' || name.back() == ' ')
        return false;
    for (const unsigned char c : name) if (c < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) return false;
    const auto stem = Lower(name.substr(0, name.find('.')));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
    if (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9') return false;
    return true;
}
bool ProjectAssets::ImportNameAvailable(const std::string& name) const
{
    if (!ValidImportName(name)) return false;
    const auto destination = m_root / "models" / AssetPathFromUtf8(name);
    return IsAssetPathWithin(destination, m_root) && !std::filesystem::exists(destination);
}

std::filesystem::path ProjectAssets::ImportModel(const std::filesystem::path& source, const std::string& name)
{
    if (!IsModel(source)) throw std::runtime_error("Import accepts .gltf and .glb models only");
    if (!ImportNameAvailable(name)) throw std::runtime_error("Model name already exists or is invalid; choose another name");
    const auto input = std::filesystem::absolute(source).lexically_normal();
    Scene validated;
    const auto index = ModelImporter::LoadAsset(validated, input);
    const std::string fingerprint = validated.modelAssets[index].fingerprint;
    const auto files = ModelImporter::SourceFiles(input);
    const auto models = m_root / "models";
    if (!IsAssetPathWithin(models, m_root)) throw std::runtime_error("Models directory is outside assets");
    std::filesystem::create_directories(models);
    static std::atomic<uint64_t> nextCandidate{0};
    std::filesystem::path staging;
    // create_directory 是排他创建；不接管已有临时目录，也不覆盖已有模型。
    for (;;) {
        staging = models / (".import-" + std::to_string(++nextCandidate));
        if (std::filesystem::create_directory(staging)) break;
    }
    try {
        for (const auto& file : files) {
            const auto relative = file.lexically_relative(input.parent_path());
            const auto target = staging / relative;
            if (!IsAssetPathWithin(file, input.parent_path()) || !IsAssetPathWithin(target, staging))
                throw std::runtime_error("Model dependency escapes its directory");
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(file, target); // 不复制无关目录内容，不覆盖。
        }
        Scene copied;
        ModelImporter::LoadAsset(copied, staging / input.filename(), {}, fingerprint);
        const auto destination = models / AssetPathFromUtf8(name);
        if (!ImportNameAvailable(name)) throw std::runtime_error("Import destination appeared; choose another name");
        std::filesystem::rename(staging, destination);
        return destination / input.filename();
    } catch (...) {
        // 只回收本次排他创建且仍在项目内的候选；不触及源文件或已有资源。
        std::error_code ignored;
        if (IsAssetPathWithin(staging, m_root)) std::filesystem::remove_all(staging, ignored);
        throw;
    }
}
