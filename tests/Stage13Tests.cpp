#include "editor/ProjectAssets.h"
#include "editor/SceneDocument.h"
#include "scene/ModelImporter.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/SceneUpdater.h"
#include "scene/Components.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
void Write(const std::filesystem::path& path, const std::string& data)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary); file << data;
    if (!file) throw std::runtime_error("fixture write failed");
}
std::string Decode(const std::string& uri)
{
    const auto start = uri.find(',') + 1;
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned value = 0; int bits = 0; std::string bytes;
    for (size_t i = start; i < uri.size() && uri[i] != '='; ++i) {
        const auto digit = alphabet.find(uri[i]);
        if (digit == std::string::npos) throw std::runtime_error("bad fixture base64");
        value = (value << 6) | static_cast<unsigned>(digit); bits += 6;
        if (bits >= 8) { bits -= 8; bytes += static_cast<char>(value >> bits); }
    }
    return bytes;
}
template<class F> bool Fails(F action)
{
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
}

bool Stage13Checks(int& assertions)
{
#define CHECK13(value) do { ++assertions; if (!(value)) { std::cerr << "Stage13 failed at " << __LINE__ << ": " << #value << '\n'; return false; } } while(false)
    const auto sourceRoot = std::filesystem::path(u8"" TEST_SOURCE_DIR);
    const auto work = sourceRoot / "build" / "project-workflow-tests";
    if (!IsAssetPathWithin(work, sourceRoot / "build")) throw std::runtime_error("unsafe test directory");
    std::filesystem::remove_all(work);
    const auto root = work / u8"项目资源";
    ProjectAssets assets(root);
    CHECK13(assets.Entries().empty());
    CHECK13(ProjectAssets::IsModel("Chair.GLB"));
    CHECK13(ProjectAssets::IsScene("Demo.SCENE.JSON"));
    CHECK13(!ProjectAssets::IsScene("Demo.json"));
    CHECK13(!ProjectAssets::ValidImportName("../escape"));
    CHECK13(!ProjectAssets::ValidImportName("CON"));
    CHECK13(!ProjectAssets::ValidImportName("x/y"));
    CHECK13(!ProjectAssets::ValidImportName("x."));
    CHECK13(ProjectAssets::ValidImportName("Chair 01"));
    CHECK13(Fails([&] { assets.Resolve("../outside.scene.json", ProjectAssetKind::Scene); }));
    CHECK13(Fails([&] { assets.Resolve("test.png", ProjectAssetKind::Model); }));

    Json external = Json::parse(Read(sourceRoot / "tests/data/model-import-test.gltf"));
    const auto model = work / "external" / u8"椅子.gltf";
    const auto buffer = Decode(external["buffers"][0]["uri"]);
    const auto image = Decode(external["images"][0]["uri"]);
    external["buffers"][0]["uri"] = "geometry/mesh.bin";
    external["images"][0]["uri"] = "textures/color.png";
    Write(model.parent_path() / "geometry/mesh.bin", buffer);
    Write(model.parent_path() / "textures/color.png", image);
    Write(model, external.dump());
    Write(model.parent_path() / "unrelated.txt", "do not copy");
    const auto files = ModelImporter::SourceFiles(model);
    CHECK13(files.size() == 3);
    const auto imported = assets.ImportModel(model, "Chair");
    CHECK13(std::filesystem::exists(imported));
    CHECK13(Read(imported.parent_path() / "geometry/mesh.bin") == buffer);
    CHECK13(Read(imported.parent_path() / "textures/color.png") == image);
    CHECK13(!std::filesystem::exists(imported.parent_path() / "unrelated.txt"));
    CHECK13(Fails([&] { assets.ImportModel(model, "Chair"); }));
    CHECK13(Read(imported) == external.dump());
    const auto renamed = assets.ImportModel(model, "Chair2");
    CHECK13(std::filesystem::exists(renamed));
    assets.Refresh();
    ProjectAssets restarted(root);
    bool listed = false;
    for (const auto& entry : restarted.Entries())
        if (entry.kind == ProjectAssetKind::Model && restarted.Root() / entry.relativePath == imported) listed = true;
    CHECK13(listed);
    // Missing, escaped and malformed dependencies never publish a resource folder.
    auto bad = external; bad["images"][0]["uri"] = "missing.png";
    Write(model.parent_path() / "bad.gltf", bad.dump());
    CHECK13(Fails([&] { assets.ImportModel(model.parent_path() / "bad.gltf", "Bad"); }));
    CHECK13(!std::filesystem::exists(root / "models/Bad"));
    bad["images"][0]["uri"] = "../outside.png";
    Write(work / "outside.png", image); Write(model.parent_path() / "bad.gltf", bad.dump());
    CHECK13(Fails([&] { assets.ImportModel(model.parent_path() / "bad.gltf", "Escaped"); }));
    CHECK13(!std::filesystem::exists(root / "models/Escaped"));
    for (const auto& entry : std::filesystem::directory_iterator(root / "models"))
        CHECK13(!AssetPathUtf8(entry.path().filename()).starts_with(".import-"));

    Scene scene = SceneDocument::EmptyScene();
    SceneDocument document;
    CHECK13(scene.registry.EntityCount() == 0 && scene.modelAssets.empty());
    CHECK13(!scene.environment.enabled && !scene.environment.background);
    CHECK13(!document.Dirty() && document.Path().empty());
    ModelImporter::Import(scene, imported);
    const auto entityCount = scene.registry.EntityCount();
    CHECK13(entityCount > 0);
    document.MarkModified();
    CHECK13(document.Dirty());
    const auto saved = root / "scenes" / u8"场景.scene.json";
    CHECK13(document.Save(scene, assets, saved));
    CHECK13(!document.Dirty() && document.Path() == saved);
    const auto original = Read(saved);
    CHECK13(Json::parse(original)["version"] == 3);
    Scene roundtrip = SceneSerializer::Load(saved);
    CHECK13(roundtrip.registry.EntityCount() == entityCount);
    CHECK13(roundtrip.modelAssets.size() == 1);
    Scene other;
    ModelImporter::Import(other, imported);
    CHECK13(other.registry.EntityCount() == entityCount && other.modelAssets.size() == 1);
    assets.Refresh();
    bool sceneListed = false;
    for (const auto& entry : assets.Entries()) if (entry.kind == ProjectAssetKind::Scene) sceneListed = true;
    CHECK13(sceneListed);
    const auto dotScene = root / "scenes/.named.scene.json";
    CHECK13(document.Save(scene, assets, dotScene));
    assets.Refresh();
    bool dotListed = false;
    for (const auto& entry : assets.Entries())
        if (assets.Root() / entry.relativePath == dotScene) dotListed = true;
    CHECK13(dotListed); // 保存允许的名称必须能在 Assets 找到，不按点前缀误隐藏。

    // Saving into another project subfolder keeps relative model references resolvable.
    CHECK13(document.Save(scene, assets, root / "another/nested/copy.scene.json"));
    CHECK13(SceneSerializer::Load(document.Path()).registry.EntityCount() == entityCount);
    document.MarkModified();
    CHECK13(!document.Save(scene, assets, std::nullopt));
    CHECK13(document.Dirty());
    CHECK13(Fails([&] { document.Save(scene, assets, work / "external.scene.json"); }));
    const auto previousPath = document.Path();
    Write(root / "blocked", "parent is a file");
    CHECK13(Fails([&] { document.Save(scene, assets, root / "blocked/new.scene.json"); }));
    CHECK13(document.Path() == previousPath && document.Dirty());
    CHECK13(Read(saved) == original);
    // 已写完候选，但目标是目录，最终发布必须失败并回收候选。
    const auto directoryTarget = root / "scenes/blocked.scene.json";
    Write(directoryTarget / "marker", "original directory");
    CHECK13(Fails([&] { document.Save(scene, assets, directoryTarget); }));
    CHECK13(document.Dirty() && document.Path() == previousPath);
    CHECK13(Read(directoryTarget / "marker") == "original directory");
    for (const auto& file : std::filesystem::directory_iterator(root / "scenes"))
        CHECK13(AssetPathUtf8(file.path().filename()).find(".saving-") == std::string::npos);
    // Invalid authored scene must not truncate an existing saved scene.
    auto invalidEntity = scene.registry.Create(); scene.registry.Add<Transform>(invalidEntity);
    scene.registry.Add<MeshRenderer>(invalidEntity, MeshRenderer{UINT32_MAX, 0});
    CHECK13(Fails([&] { document.Save(scene, assets, saved); }));
    CHECK13(Read(saved) == original && document.Path() == previousPath && document.Dirty());
    scene.registry.Destroy(invalidEntity);

    document.RequestNew();
    CHECK13(document.NeedsConfirmation());
    CHECK13(!document.Advance(scene, assets, [] { return std::optional<std::filesystem::path>{}; }));
    document.Decide(SceneDecision::Cancel);
    CHECK13(!document.HasPending() && document.Dirty());
    CHECK13(scene.registry.EntityCount() == entityCount);
    document.RequestNew(); document.Decide(SceneDecision::Save);
    CHECK13(document.Advance(scene, assets, [] { throw std::runtime_error("named Save must not ask Save As"); return std::optional<std::filesystem::path>{}; }));
    CHECK13(scene.registry.EntityCount() == 0 && document.Path().empty() && !document.Dirty());
    document.RequestOpen(saved);
    CHECK13(document.Advance(scene, assets, [] { return std::optional<std::filesystem::path>{}; }));
    CHECK13(scene.registry.EntityCount() == entityCount && document.Path() == saved);
    // Camera and simulation changes do not dirty the document by themselves.
    scene.camera.position.x += 1;
    SceneUpdater updater; updater.Update(scene, 0.3f);
    CHECK13(!document.Dirty());
    document.MarkModified();
    document.RequestOpen(root / "missing.scene.json"); document.Decide(SceneDecision::Discard);
    CHECK13(Fails([&] { document.Advance(scene, assets, [] { return std::optional<std::filesystem::path>{}; }); }));
    CHECK13(scene.registry.EntityCount() == entityCount && document.Dirty() && document.Path() == saved && !document.HasPending());
    document.RequestNew(); document.Decide(SceneDecision::Discard);
    CHECK13(document.Advance(scene, assets, [] { return std::optional<std::filesystem::path>{}; }));
    document.MarkModified(); document.RequestOpen(saved); document.Decide(SceneDecision::Save);
    CHECK13(!document.Advance(scene, assets, [] { return std::optional<std::filesystem::path>{}; }));
    CHECK13(document.Dirty() && document.Path().empty() && !document.HasPending());
    document.RequestOpen(saved); document.Decide(SceneDecision::Save);
    CHECK13(Fails([&] { document.Advance(scene, assets, [&] { return std::optional(work / "outside.scene.json"); }); }));
    CHECK13(document.Dirty() && document.Path().empty() && scene.registry.EntityCount() == 0 && !document.HasPending());
    document.RequestOpen(saved); document.Decide(SceneDecision::Save);
    const auto beforeOpenSave = root / "scenes/before-open.scene.json";
    CHECK13(document.Advance(scene, assets, [&] { return std::optional(beforeOpenSave); }));
    CHECK13(SceneSerializer::Load(beforeOpenSave).registry.EntityCount() == 0);
    CHECK13(scene.registry.EntityCount() == entityCount && document.Path() == saved && !document.Dirty());
    // Existing v3 fingerprint rejection still preserves an actively edited scene.
    auto changed = external; changed["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = 0.1;
    Write(imported, changed.dump());
    document.MarkModified(); document.RequestOpen(saved); document.Decide(SceneDecision::Discard);
    CHECK13(Fails([&] { document.Advance(scene, assets, [] { return std::optional<std::filesystem::path>{}; }); }));
    CHECK13(document.Dirty() && document.Path() == saved && scene.registry.EntityCount() == entityCount);
    Write(imported, external.dump());
    return true;
#undef CHECK13
}
