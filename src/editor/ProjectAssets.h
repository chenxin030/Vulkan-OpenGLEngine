#pragma once

#include <filesystem>
#include <string>
#include <vector>

enum class ProjectAssetKind { Directory, Model, Scene };
struct ProjectAssetEntry
{
    std::filesystem::path relativePath; // 相对于项目 assets/，不是 Scene 内的 assetId。
    ProjectAssetKind kind;
};

std::string AssetPathUtf8(const std::filesystem::path& path);
std::filesystem::path AssetPathFromUtf8(const std::string& text);
// weakly_canonical 同时约束现存文件和尚未创建的 Save As 路径，拒绝目录穿越/外部链接。
bool IsAssetPathWithin(const std::filesystem::path& path, const std::filesystem::path& root);

class ProjectAssets
{
public:
    ProjectAssets() = default;
    explicit ProjectAssets(const std::filesystem::path& root) { Initialize(root); }
    void Initialize(const std::filesystem::path& root);
    void Refresh();
    const std::filesystem::path& Root() const { return m_root; }
    const std::vector<ProjectAssetEntry>& Entries() const { return m_entries; }
    std::filesystem::path Resolve(const std::filesystem::path& path, ProjectAssetKind kind) const;
    bool ImportNameAvailable(const std::string& name) const;
    // 仅写项目文件。解析、复制、再验证候选后发布，不访问当前 Scene。
    std::filesystem::path ImportModel(const std::filesystem::path& source, const std::string& name);
    static bool IsModel(const std::filesystem::path& path);
    static bool IsScene(const std::filesystem::path& path);
    static bool ValidImportName(const std::string& name);
private:
    std::filesystem::path m_root;
    std::vector<ProjectAssetEntry> m_entries;
};
