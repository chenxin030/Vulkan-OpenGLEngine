#pragma once

#include "editor/ProjectAssets.h"
#include <optional>

struct AssetBrowserActions
{
    bool importModel = false, refresh = false;
    std::optional<std::filesystem::path> openScene;
};

// 只保存浏览位置/选中项，和 Scene 的 Entity 选择相互独立。
class AssetBrowser
{
public:
    AssetBrowserActions Draw(const ProjectAssets& assets, bool playing);
    void Reveal(const ProjectAssets& assets, const std::filesystem::path& path);
private:
    void DrawDirectory(const ProjectAssets& assets, const std::filesystem::path& directory);
    std::filesystem::path m_directory, m_selected;
    bool m_resetScroll = false;
};
