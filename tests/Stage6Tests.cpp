#include "scene/Scene.h"
#include "scene/Components.h"
#include "scene/ModelImporter.h"
#include "scene/SceneSerializer.h"
#include "scene/SceneUpdater.h"
#include "scene/RenderSnapshot.h"
#include "scene/ContentFingerprint.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace {
using Json = nlohmann::json;
std::string Utf8(const std::filesystem::path& p)
{
    auto value = p.generic_u8string();
    return { reinterpret_cast<const char*>(value.data()), value.size() };
}
Json ReadJson(const std::filesystem::path& path)
{
    Json value; std::ifstream file(path); file >> value; return value;
}
void WriteJson(const std::filesystem::path& path, const Json& value)
{
    std::ofstream file(path); file << value.dump(2);
}
std::vector<uint8_t> Decode(const std::string& uri)
{
    std::vector<uint8_t> bytes;
    uint32_t accumulator = 0; int bits = 0;
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = uri.find(',') + 1; i < uri.size() && uri[i] != '='; ++i) {
        accumulator = (accumulator << 6) | static_cast<uint32_t>(alphabet.find(uri[i]));
        bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push_back(static_cast<uint8_t>(accumulator >> bits)); }
    }
    return bytes;
}
void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
std::string Rejection(const std::filesystem::path& path)
{
    try { SceneSerializer::Load(path); return {}; }
    catch (const std::exception& error) { return error.what(); }
}
struct Files
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        std::filesystem::path(u8"rebuild-stage6-中文") /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::vector<std::filesystem::path> paths;
    Files() { std::filesystem::create_directories(root); }
    std::filesystem::path File(const std::filesystem::path& name)
    {
        auto path = root / name; paths.push_back(path); return path;
    }
    ~Files()
    {
        std::error_code error;
        for (auto i = paths.rbegin(); i != paths.rend(); ++i) std::filesystem::remove(*i, error);
        std::filesystem::remove(root, error);
        std::filesystem::remove(root.parent_path(), error); // 仅尝试移除空目录。
    }
};
}

bool Stage6Checks(int& assertions)
{
#define VERIFY(condition) do { ++assertions; if (!(condition)) { \
    std::cerr << "Stage6 check failed at " << __LINE__ << ": " #condition << '\n'; return false; } } while (false)
    ContentFingerprint sha;
    VERIFY(sha.Hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    sha.Update({ reinterpret_cast<const uint8_t*>(abc.data()), abc.size() });
    VERIFY(sha.Hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ContentFingerprint boundary;
    const std::string message = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (unsigned char byte : message) boundary.Update({ &byte, 1 });
    VERIFY(boundary.Hex() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    Files files;
    const auto aPath = files.File(std::filesystem::path(u8"椅子.gltf"));
    const auto bPath = files.File("b.gltf");
    const auto scenePath = files.File("models.scene.json");
    const auto invalidPath = files.File("invalid.scene.json");
    const auto roundTripPath = files.File("roundtrip.scene.json");
    const auto fixture = std::filesystem::path(TEST_SOURCE_DIR) / "tests/data/model-import-test.gltf";
    const Json base = ReadJson(fixture);
    Json a = base;
    auto fallbackPrimitive = a["meshes"][0]["primitives"][0];
    fallbackPrimitive.erase("material");
    a["meshes"][0]["primitives"].push_back(fallbackPrimitive);
    WriteJson(aPath, a);
    Json b = a;
    b["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {0.1f, 0.9f, 0.2f, 1.0f};
    WriteJson(bPath, b);
    Scene scene;
    auto asset = ModelImporter::LoadAsset(scene, aPath, "chair-persistent");
    auto first = ModelImporter::InstantiateAsset(scene, asset).front();
    auto second = ModelImporter::Import(scene, aPath).front();
    ModelImporter::Import(scene, bPath);
    VERIFY(scene.modelAssets.size() == 2);
    VERIFY(scene.modelAssets[0].id == "chair-persistent");
    VERIFY(scene.materials.MaterialCount() == 5);
    const auto& defaultSource = scene.modelMaterialSources.at(scene.modelAssets[0].materialIndices[1]);
    VERIFY(defaultSource.sourceMaterial == -1);
    const auto& fallback = scene.materials.GetMaterial(scene.modelAssets[0].materialIndices[1]);
    VERIFY(fallback.metallicFactor == 1.0f && fallback.roughnessFactor == 1.0f);
    VERIFY(fallback.baseColorTexture == UINT32_MAX);
    scene.registry.TryGet<Transform>(first)->local[3][0] = -3;
    scene.registry.TryGet<Transform>(second)->local[3][0] = 3;
    Entity removed;
    scene.registry.ForEach<MeshRenderer>([&](Entity entity, MeshRenderer& mesh) {
        if (scene.modelMaterialSources.at(mesh.materialIndex).sourceMaterial != -1) return;
        Entity current = entity;
        while (const Parent* parent = scene.registry.TryGet<Parent>(current)) {
            if (parent->entity == first) { removed = entity; return; }
            current = parent->entity;
        }
    });
    VERIFY(removed.IsValid());
    scene.DestroyEntitySubtree(removed);
    SceneUpdater updater;
    updater.Update(scene, 0);
    const auto before = BuildRenderSnapshot(scene);
    VERIFY(before.drawItems.size() == 5);
    SceneSerializer::Save(scene, scenePath);
    Json saved = ReadJson(scenePath);
    VERIFY(saved["version"] == 3);
    VERIFY(saved["modelAssets"][0]["id"] == "chair-persistent");
    VERIFY(saved["modelAssets"][0]["path"] == Utf8(aPath.filename()));
    VERIFY(saved["modelAssets"][0]["fingerprint"] == scene.modelAssets[0].fingerprint);
    std::swap(saved["modelAssets"][0], saved["modelAssets"][1]);
    WriteJson(scenePath, saved);
    Scene restored = SceneSerializer::Load(scenePath);
    VERIFY(restored.modelAssets[1].id == "chair-persistent");
    VERIFY(restored.registry.EntityCount() == scene.registry.EntityCount());
    updater.Update(restored, 0);
    auto after = BuildRenderSnapshot(restored);
    VERIFY(after.drawItems.size() == before.drawItems.size());
    bool same = true;
    for (size_t i = 0; i < before.drawItems.size(); ++i) {
        same &= before.drawItems[i].worldTransform == after.drawItems[i].worldTransform;
        same &= before.drawItems[i].tint == after.drawItems[i].tint;
        same &= before.drawItems[i].metallicFactor == after.drawItems[i].metallicFactor;
    }
    VERIFY(same);
    VERIFY(before.drawItems[0].meshIndex != after.drawItems[0].meshIndex);
    SceneSerializer::Save(restored, roundTripPath);
    VERIFY(ReadJson(roundTripPath)["modelAssets"][1]["id"] == "chair-persistent");

    auto bad = saved;
    bad["modelAssets"].push_back(bad["modelAssets"][0]);
    WriteJson(invalidPath, bad); VERIFY(!Rejection(invalidPath).empty());
    bad = saved;
    for (auto& entity : bad["entities"]) if (entity.contains("mesh")) {
        entity["mesh"]["asset"] = "missing"; break;
    }
    WriteJson(invalidPath, bad); VERIFY(!Rejection(invalidPath).empty());
    bad = saved;
    for (auto& entity : bad["entities"]) if (entity.contains("mesh")) {
        entity["mesh"]["sourceMesh"] = uint64_t(UINT32_MAX) + 1; break;
    }
    WriteJson(invalidPath, bad); VERIFY(!Rejection(invalidPath).empty());
    bad = saved;
    for (auto& entity : bad["entities"]) if (entity.contains("material")) {
        entity["material"] = {{"asset", "chair-persistent"}, {"default", false}}; break;
    }
    WriteJson(invalidPath, bad); VERIFY(!Rejection(invalidPath).empty());
    bad = saved; bad["entities"][0]["parent"] = bad["entities"][0]["id"];
    WriteJson(invalidPath, bad); VERIFY(!Rejection(invalidPath).empty());
    bad = saved; bad["modelAssets"][0]["path"] = "missing.gltf";
    WriteJson(invalidPath, bad);
    const auto preserved = restored.registry.EntityCount();
    try { restored = SceneSerializer::Load(invalidPath); } catch (const std::exception&) {}
    VERIFY(restored.registry.EntityCount() == preserved);
    bad = saved; bad["modelAssets"][0]["id"] = std::string("bad\0id", 6);
    WriteJson(invalidPath, bad); VERIFY(!Rejection(invalidPath).empty());
    bool invalidIdRejected = false;
    try { Scene fresh; ModelImporter::LoadAsset(fresh, aPath, std::string("bad\0id", 6)); }
    catch (const std::exception&) { invalidIdRejected = true; }
    VERIFY(invalidIdRejected);

    // 单一格式契约：旧版本、未知版本和缺失版本都拒绝，并保留调用者的场景。
    for (int unsupportedVersion : {1, 2, 4}) {
        bad = saved; bad["version"] = unsupportedVersion;
        WriteJson(invalidPath, bad);
        VERIFY(Rejection(invalidPath).find("only version 3") != std::string::npos);
        try { restored = SceneSerializer::Load(invalidPath); } catch (const std::exception&) {}
        VERIFY(restored.registry.EntityCount() == preserved);
    }
    bad = saved; bad.erase("version"); WriteJson(invalidPath, bad);
    VERIFY(Rejection(invalidPath).find("only version 3") != std::string::npos);

    // 只剩资产没有实例，仍能往返并重新实例化。
    Scene onlyAssets;
    ModelImporter::LoadAsset(onlyAssets, aPath, "empty-asset");
    SceneSerializer::Save(onlyAssets, roundTripPath);
    onlyAssets = SceneSerializer::Load(roundTripPath);
    VERIFY(onlyAssets.registry.EntityCount() == 0 && onlyAssets.modelAssets.size() == 1);
    ModelImporter::InstantiateAsset(onlyAssets, onlyAssets.FindModelAssetIndex("empty-asset"));
    VERIFY(BuildRenderSnapshot(onlyAssets).drawItems.size() == 2);

    Scene builtin;
    const auto built = builtin.registry.Create();
    builtin.registry.Add<Transform>(built);
    builtin.registry.Add<MeshRenderer>(built, MeshRenderer{MeshLibrary::kCube, 0});
    SceneSerializer::Save(builtin, roundTripPath);
    const auto builtinFile = ReadJson(roundTripPath);
    VERIFY(builtinFile["entities"][0]["mesh"]["builtin"] == "cube");
    VERIFY(SceneSerializer::Load(roundTripPath).registry.EntityCount() == 1);
    const auto unknown = builtin.meshes.Append(builtin.meshes.Get(MeshLibrary::kCube));
    builtin.registry.TryGet<MeshRenderer>(built)->meshIndex = unknown;
    bool rejected = false;
    try { SceneSerializer::Save(builtin, roundTripPath); } catch (const std::exception&) { rejected = true; }
    VERIFY(rejected && ReadJson(roundTripPath) == builtinFile);

    const auto externalPath = files.File("external.gltf");
    const auto bufferPath = files.File("mesh.bin"), imagePath = files.File("tex.png");
    const auto externalScenePath = files.File("external.scene.json");
    auto buffer = Decode(base["buffers"][0]["uri"].get<std::string>());
    auto png = Decode(base["images"][0]["uri"].get<std::string>());
    auto external = base;
    external["buffers"][0]["uri"] = "mesh.bin"; external["images"][0]["uri"] = "tex.png";
    WriteBytes(bufferPath, buffer); WriteBytes(imagePath, png); WriteJson(externalPath, external);
    Scene externalScene;
    ModelImporter::Import(externalScene, externalPath);
    SceneSerializer::Save(externalScene, externalScenePath);
    VERIFY(SceneSerializer::Load(externalScenePath).modelAssets.size() == 1);
    auto changed = buffer; changed[0] ^= 1; WriteBytes(bufferPath, changed);
    VERIFY(Rejection(externalScenePath).find("content changed") != std::string::npos);
    WriteBytes(bufferPath, buffer);
    changed = png; changed.push_back(0); WriteBytes(imagePath, changed);
    VERIFY(Rejection(externalScenePath).find("content changed") != std::string::npos);
    WriteBytes(imagePath, png);
    { std::ofstream file(externalPath, std::ios::app); file << '\n'; }
    VERIFY(Rejection(externalScenePath).find("content changed") != std::string::npos);
    // Save 不接受磁盘新基线；进程内缓存仍可创建旧版本实例。
    const auto fingerprint = externalScene.modelAssets[0].fingerprint;
    ModelImporter::Import(externalScene, externalPath);
    VERIFY(externalScene.modelAssets[0].fingerprint == fingerprint);
    SceneSerializer::Save(externalScene, externalScenePath);
    VERIFY(Rejection(externalScenePath).find("content changed") != std::string::npos);
    WriteJson(externalPath, external);
    VERIFY(SceneSerializer::Load(externalScenePath).registry.EntityCount() == externalScene.registry.EntityCount());
    // 整份资产目录迁移：位置变化不改变实际导入字节的指纹。
    const auto movedFolder = files.File("moved"); std::filesystem::create_directory(movedFolder);
    const auto movedModel = files.File("moved/external.gltf");
    const auto movedBuffer = files.File("moved/mesh.bin"), movedImage = files.File("moved/tex.png");
    WriteJson(movedModel, external); WriteBytes(movedBuffer, buffer); WriteBytes(movedImage, png);
    auto relocatedExternal = ReadJson(externalScenePath);
    relocatedExternal["modelAssets"][0]["path"] = "moved/external.gltf";
    WriteJson(invalidPath, relocatedExternal);
    auto movedScene = SceneSerializer::Load(invalidPath);
    VERIFY(movedScene.modelAssets[0].fingerprint == fingerprint);
    VERIFY(movedScene.registry.EntityCount() == externalScene.registry.EntityCount());

    const auto folder = files.File("save-as"); std::filesystem::create_directory(folder);
    const auto relocated = files.File("save-as/relocated.scene.json");
    SceneSerializer::Save(scene, relocated);
    VERIFY(ReadJson(relocated)["modelAssets"][0]["path"] == "../" + Utf8(aPath.filename()));
    VERIFY(SceneSerializer::Load(relocated).modelAssets[0].id == "chair-persistent");
    const auto originalCwd = std::filesystem::current_path();
    struct RestoreCwd { std::filesystem::path path; ~RestoreCwd() { std::filesystem::current_path(path); } } cwd{originalCwd};
    std::filesystem::current_path(folder);
    VERIFY(SceneSerializer::Load(scenePath).registry.EntityCount() == scene.registry.EntityCount());
    std::filesystem::current_path(originalCwd);
    return true;
#undef VERIFY
}
