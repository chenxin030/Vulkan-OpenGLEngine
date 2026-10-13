#include "scene/Registry.h"
#include "platform/InputState.h"
#include "scene/Components.h"
#include "scene/RenderSnapshot.h"
#include "scene/Scene.h"
#include "scene/SceneUpdater.h"
#include "scene/SceneSerializer.h"
#include "scene/ModelImporter.h"
#include "scene/ContentFingerprint.h"

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

bool Stage6Checks(int& assertions);
bool Stage7Checks(int& assertions);
bool Stage8Checks(int& assertions);
bool Stage9Checks(int& assertions);
bool Stage10Checks(int& assertions);

namespace {

int g_checks = 0;

#define CHECK(condition)                                                        \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(condition)) {                                                      \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__      \
                      << ": " #condition << '\n';                               \
            return false;                                                        \
        }                                                                        \
    } while (false)

struct Position
{
    int value = 0;
};

struct Velocity
{
    int value = 0;
};

bool EntityLifecycleAndComponentStorage()
{
    Registry registry;
    const Entity first = registry.Create();
    const Entity second = registry.Create();

    registry.Add<Position>(first, Position{ 10 });
    registry.Add<Position>(second, Position{ 20 });
    registry.Add<Velocity>(first, Velocity{ 3 });

    CHECK(registry.EntityCount() == 2);
    CHECK(registry.IsAlive(first));
    CHECK(registry.Has<Position>(first));
    CHECK(registry.TryGet<Position>(first)->value == 10);

    int matchingEntities = 0;
    int sum = 0;
    bool matchedExpectedEntity = true;
    registry.ForEach<Position, Velocity>([&](Entity entity, Position& position, Velocity& velocity) {
        matchedExpectedEntity = matchedExpectedEntity && entity == first;
        sum += position.value + velocity.value;
        ++matchingEntities;
    });
    CHECK(matchedExpectedEntity);
    CHECK(matchingEntities == 1);
    CHECK(sum == 13);

    registry.Remove<Velocity>(first);
    CHECK(!registry.Has<Velocity>(first));

    registry.Destroy(first);
    CHECK(!registry.IsAlive(first));
    CHECK(registry.TryGet<Position>(first) == nullptr);
    CHECK(registry.EntityCount() == 1);

    const Entity replacement = registry.Create();
    CHECK(replacement.index == first.index);
    CHECK(replacement.generation != first.generation);
    CHECK(registry.IsAlive(replacement));
    CHECK(!registry.IsAlive(first));
    return true;
}


// 陈旧的句柄无法分离重用的实体组件
bool StaleHandlesCannotDetachReusedEntityComponents()
{
    Registry registry;
    const Entity stale = registry.Create();
    registry.Add<Position>(stale, Position{ 1 });
    registry.Destroy(stale);

    const Entity current = registry.Create();
    registry.Add<Position>(current, Position{ 42 });

    registry.Remove<Position>(stale);
    CHECK(registry.Has<Position>(current));
    CHECK(registry.TryGet<Position>(current)->value == 42);
    return true;
}

// 无效的实体操作会被拒绝或忽略
bool InvalidEntityOperationsAreRejectedOrIgnored()
{
    Registry registry;
    const Entity dead = registry.Create();
    registry.Destroy(dead);

    bool rejected = false;
    try {
        registry.Add<Position>(dead, Position{ 7 });
    }
    catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);

    // Destroy on a stale handle is intentionally harmless.
    registry.Destroy(dead);
    CHECK(registry.EntityCount() == 0);
    return true;
}

// 仅在索引重用后枚举实体列表中的存活实体
bool ForEachEntityListsOnlyLiveEntitiesAfterIndexReuse()
{
    Registry registry;
    const Entity removed = registry.Create();
    const Entity retained = registry.Create();
    registry.Destroy(removed);
    const Entity replacement = registry.Create();

    std::vector<Entity> entities;
    registry.ForEachEntity([&entities](Entity entity) { entities.push_back(entity); });

    CHECK(entities.size() == 2);
    CHECK(entities[0] == replacement);
    CHECK(entities[1] == retained);
    CHECK(entities[0] != removed);
    return true;
}

// 编辑器捕获时会禁止摄像头并退出输入
bool EditorCaptureSuppressesCameraAndQuitInput()
{
    InputState input;
    input.moveAxis = glm::vec3(1.0f, 1.0f, -1.0f);
    input.sprint = true;
    input.quitRequested = true;
    input.lookActive = true;
    input.lookDelta = glm::vec2(12.0f, -8.0f);

    FilterCapturedEditorInput(input, EditorInputCapture{ true, true });

    CHECK(input.moveAxis == glm::vec3(0.0f));
    CHECK(!input.sprint);
    CHECK(!input.quitRequested);
    CHECK(!input.lookActive);
    CHECK(input.lookDelta == glm::vec2(0.0f));
    return true;
}

// 场景更新器生成渲染快照
bool SceneUpdaterProducesRenderSnapshot()
{
    Scene scene;
    const Entity entity = scene.registry.Create();
    Transform transform;
    transform.local = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
    transform.world = transform.local;
    scene.registry.Add<Transform>(entity, transform);
    scene.registry.Add<MeshRenderer>(entity, MeshRenderer{ MeshLibrary::kCube });
    scene.registry.Add<Spin>(entity, Spin{ glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, 0.0f });

    SceneUpdater updater;
    updater.Update(scene, 0.5f);
    const RenderSnapshot snapshot = BuildRenderSnapshot(scene);

    CHECK(scene.registry.TryGet<Spin>(entity)->angle == 0.5f);
    CHECK(snapshot.meshes == &scene.meshes);
    CHECK(snapshot.drawItems.size() == 1);
    CHECK(std::abs(glm::length(snapshot.lightDirection) - 1.0f) < 0.001f);
    CHECK(snapshot.lightColor == DirectionalLightColor());
    CHECK(std::isfinite(snapshot.lightViewProjection[0][0]));
    CHECK(snapshot.drawItems[0].meshIndex == MeshLibrary::kCube);
    CHECK(snapshot.drawItems[0].metallicFactor == 0.0f);
    CHECK(std::abs(snapshot.drawItems[0].roughnessFactor - 0.8f) < 0.001f);
    CHECK(glm::length(scene.meshes.Get(MeshLibrary::kCube).vertices[0].normal) > 0.99f);
    CHECK(scene.meshes.Get(MeshLibrary::kCube).vertices[0].normal.z > 0.99f);
    CHECK(scene.meshes.Get(MeshLibrary::kPlane).vertices[0].normal.y > 0.99f);
    CHECK(std::abs(snapshot.drawItems[0].worldTransform[3][0] - 2.0f) < 0.001f);
    return true;
}

bool RegistryCopiesAreIndependent()
{
    Registry source;
    const Entity original = source.Create();
    source.Add<Position>(original, Position{ 17 });
    Registry clone = source;
    clone.TryGet<Position>(original)->value = 99;
    CHECK(source.TryGet<Position>(original)->value == 17);
    CHECK(clone.TryGet<Position>(original)->value == 99);
    const Entity cloneOnly = clone.Create();
    CHECK(source.EntityCount() == 1);
    CHECK(clone.EntityCount() == 2);
    CHECK(clone.IsAlive(cloneOnly));
    return true;
}

bool SpritesEnterRenderSnapshotAndSceneRoundTrips()
{
    Scene scene = BuildDemo2DScene();
    const Entity entity = scene.registry.Create();
    Transform transform;
    transform.local = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, -1.0f, 0.0f));
    transform.world = transform.local;
    scene.registry.Add<Transform>(entity, transform);
    scene.registry.Add<SpriteRenderer>(entity, SpriteRenderer{ glm::vec3(0.2f, 0.4f, 0.8f) });
    RenderSnapshot snapshot = BuildRenderSnapshot(scene);
    CHECK(snapshot.drawItems.size() == 4);
    CHECK(snapshot.drawItems.back().meshIndex == MeshLibrary::kQuad);
    CHECK(snapshot.drawItems.back().tint == glm::vec3(0.2f, 0.4f, 0.8f));
    CHECK(snapshot.drawItems.back().unlit);
    CHECK(scene.camera.projection == CameraProjection::Orthographic);

    const auto path = std::filesystem::temp_directory_path() / "rebuild-scene-test.json";
    SceneSerializer::Save(scene, path);
    Scene loaded = SceneSerializer::Load(path);
    std::filesystem::remove(path);
    CHECK(loaded.registry.EntityCount() == scene.registry.EntityCount());
    CHECK(loaded.camera.projection == CameraProjection::Orthographic);
    CHECK(loaded.registry.TryGet<SpriteRenderer>(entity) != nullptr);
    CHECK(loaded.registry.TryGet<SpriteRenderer>(entity)->tint == glm::vec3(0.2f, 0.4f, 0.8f));
    CHECK(std::abs(loaded.registry.TryGet<Transform>(entity)->local[3][0] - 4.0f) < 0.001f);
    return true;
}

bool GltfModelImportsAndSceneRoundTrips()
{
    Scene scene = BuildDemo2DScene();
    const auto modelPath = std::filesystem::path(TEST_SOURCE_DIR) / "tests/data/model-import-test.gltf";
    const std::vector<Entity> roots = ModelImporter::Import(scene, modelPath);
    CHECK(roots.size() == 1);
    CHECK(scene.modelAssets.size() == 1);
    CHECK(!scene.modelAssets.front().sourcePath.empty());
    CHECK(scene.registry.TryGet<Name>(roots[0]) != nullptr);
    CHECK(scene.registry.TryGet<Name>(roots[0])->value == "model-import-test Instance");
    CHECK(scene.materials.TextureCount() == 1);
    CHECK(scene.materials.GetTexture(0).width == 1);
    CHECK(std::abs(scene.materials.GetMaterial(1).metallicFactor - 0.25f) < 0.001f);
    CHECK(std::abs(scene.materials.GetMaterial(1).roughnessFactor - 0.7f) < 0.001f);
    CHECK(std::abs(scene.materials.GetMaterial(1).baseColorFactor.r - 0.8f) < 0.001f);
    CHECK(std::abs(scene.materials.GetMaterial(1).baseColorFactor.g - 0.4f) < 0.001f);
    RenderSnapshot snapshot = BuildRenderSnapshot(scene);
    CHECK(!snapshot.drawItems.empty());
    CHECK(snapshot.drawItems.back().textureIndex == 0);
    CHECK(std::abs(snapshot.drawItems.back().metallicFactor - 0.25f) < 0.001f);
    CHECK(std::abs(snapshot.drawItems.back().roughnessFactor - 0.7f) < 0.001f);
    CHECK(std::abs(glm::length(snapshot.meshes->Get(snapshot.drawItems.back().meshIndex).vertices[0].normal) - 1.0f) < 0.001f);
    CHECK(snapshot.meshes->Get(snapshot.drawItems.back().meshIndex).vertices[0].normal.z > 0.99f);

    const auto scenePath = std::filesystem::path(TEST_SOURCE_DIR) / "tests/data/model-roundtrip.scene.json";
    SceneSerializer::Save(scene, scenePath);
    Scene loaded = SceneSerializer::Load(scenePath);
    std::filesystem::remove(scenePath);
    CHECK(loaded.modelAssets.size() == 1);
    CHECK(loaded.materials.TextureCount() == 1);
    CHECK(loaded.registry.EntityCount() == scene.registry.EntityCount());
    CHECK(BuildRenderSnapshot(loaded).drawItems.back().textureIndex == 0);

    const uint32_t meshCount = scene.meshes.Count();
    const auto malformedPath = std::filesystem::temp_directory_path() / "rebuild-malformed-model.gltf";
    { std::ofstream malformed(malformedPath); malformed << "not glTF"; }
    bool rejected = false;
    try { ModelImporter::Import(scene, malformedPath); }
    catch (const std::exception&) { rejected = true; }
    std::filesystem::remove(malformedPath);
    CHECK(rejected);
    CHECK(scene.meshes.Count() == meshCount);
    return true;
}

} // namespace

bool EmissiveMaterialsEnterSnapshotsAndRoundTrip()
{
    CHECK(SurfaceMaterial{}.emissive == glm::vec3(0.0f));
    CHECK(DrawItem{}.emissive == glm::vec3(0.0f));
    const glm::vec3 expected(0.8f, 0.2f, 0.1f);
    const auto fixture = std::filesystem::path(TEST_SOURCE_DIR) / "tests/data/model-import-test.gltf";
    nlohmann::json model;
    { std::ifstream file(fixture); file >> model; }
    model["materials"][0]["emissiveFactor"] = { expected.r, expected.g, expected.b };
    const auto directory = std::filesystem::temp_directory_path() / "rebuild-emissive-check";
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(directory / "emissive.gltf", error);
            std::filesystem::remove(directory / "emissive.scene.json", error);
            std::filesystem::remove(directory, error);
        }
    } cleanup{ directory };
    const auto modelPath = directory / "emissive.gltf";
    { std::ofstream file(modelPath); file << model.dump(2); }

    Scene scene;
    ModelImporter::Import(scene, modelPath);
    CHECK(glm::length(scene.materials.GetMaterial(1).emissive - expected) < 0.0001f);
    CHECK(glm::length(BuildRenderSnapshot(scene).drawItems.back().emissive - expected) < 0.0001f);
    Scene runtime(scene);
    CHECK(glm::length(BuildRenderSnapshot(runtime).drawItems.back().emissive - expected) < 0.0001f);
    const auto scenePath = directory / "emissive.scene.json";
    SceneSerializer::Save(scene, scenePath);
    Scene loaded = SceneSerializer::Load(scenePath);
    CHECK(glm::length(loaded.materials.GetMaterial(1).emissive - expected) < 0.0001f);
    CHECK(glm::length(BuildRenderSnapshot(loaded).drawItems.back().emissive - expected) < 0.0001f);

    Scene missingFactor;
    ModelImporter::Import(missingFactor, fixture);
    CHECK(missingFactor.materials.GetMaterial(1).emissive == glm::vec3(0.0f));
    model.erase("materials");
    model["meshes"][0]["primitives"][0].erase("material");
    { std::ofstream file(modelPath); file << model.dump(2); }
    Scene missingMaterial;
    ModelImporter::Import(missingMaterial, modelPath);
    CHECK(missingMaterial.materials.GetMaterial(1).emissive == glm::vec3(0.0f));
    CHECK(missingMaterial.modelMaterialSources.at(1).sourceMaterial == -1);
    CHECK(BuildRenderSnapshot(BuildDemo2DScene()).drawItems.front().emissive == glm::vec3(0.0f));
    return true;
}

bool ModelAssetsReuseResourcesAndKeepInstancesIndependent()
{
    const auto fixture = std::filesystem::path(TEST_SOURCE_DIR) / "tests/data/model-import-test.gltf";
    const auto directory = std::filesystem::temp_directory_path() / "rebuild-model-asset-check";
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::error_code error;
            for (const char* name : { "model.gltf", "instances.scene.json" })
                std::filesystem::remove(directory / name, error);
            std::filesystem::remove(directory, error);
        }
    } cleanup{ directory };
    const auto modelPath = directory / "model.gltf";
    nlohmann::json model;
    { std::ifstream file(fixture); file >> model; }
    // 多根节点与多个 primitive 验证模板内索引，而非只检查单个实体数。
    model["nodes"].push_back({ {"name", "Other root"}, {"mesh", 0} });
    model["scenes"][0]["nodes"].push_back(2);
    model["meshes"][0]["primitives"].push_back(model["meshes"][0]["primitives"][0]);
    { std::ofstream file(modelPath); file << model.dump(2); }

    Scene scene;
    const uint32_t firstTexture = scene.materials.TextureCount();
    const uint32_t asset = ModelImporter::LoadAsset(scene, modelPath);
    CHECK(scene.registry.EntityCount() == 0);
    CHECK(scene.modelAssets.size() == 1);
    CHECK(scene.modelAssets[asset].nodes.size() == 3);
    CHECK(scene.modelAssets[asset].meshIndices.size() == 2);
    CHECK(scene.modelAssets[asset].textureIndices.size() == 1);
    CHECK(scene.modelAssets[asset].textureIndices[0] == firstTexture);
    CHECK(scene.materials.GetTexture(scene.modelAssets[asset].textureIndices[0]).IsValid());
    const auto meshCount = scene.meshes.Count();
    const auto materialCount = scene.materials.MaterialCount();
    const auto textureCount = scene.materials.TextureCount();
    const auto meshRevision = scene.meshes.Revision();
    const auto materialRevision = scene.materials.Revision();
    const auto first = ModelImporter::InstantiateAsset(scene, asset);
    const auto second = ModelImporter::Import(scene, directory / "." / "model.gltf");
    CHECK(first.size() == 1 && second.size() == 1);
    CHECK(first[0] != second[0]);
    CHECK(scene.registry.EntityCount() == 16);
    CHECK(scene.modelAssets.size() == 1);
    CHECK(scene.meshes.Count() == meshCount);
    CHECK(scene.materials.MaterialCount() == materialCount);
    CHECK(scene.materials.TextureCount() == textureCount);
    CHECK(scene.meshes.Revision() == meshRevision);
    CHECK(scene.materials.Revision() == materialRevision);
    scene.registry.TryGet<Transform>(first[0])->local[3][0] = 5.0f;
    scene.registry.TryGet<Name>(first[0])->value = "Edited instance";
    SceneUpdater updater;
    updater.Update(scene, 0.0f);
    const auto snapshot = BuildRenderSnapshot(scene);
    CHECK(snapshot.drawItems.size() == 8);
    CHECK(snapshot.drawItems[0].meshIndex == snapshot.drawItems[4].meshIndex);
    CHECK(snapshot.drawItems[0].textureIndex == snapshot.drawItems[4].textureIndex);
    CHECK(std::abs(snapshot.drawItems[0].worldTransform[3][0] - 6.0f) < 0.001f);
    CHECK(std::abs(snapshot.drawItems[4].worldTransform[3][0] - 1.0f) < 0.001f);
    CHECK(std::abs(snapshot.drawItems[2].worldTransform[3][0] - 5.0f) < 0.001f);
    CHECK(std::abs(snapshot.drawItems[6].worldTransform[3][0]) < 0.001f);
    CHECK(!scene.registry.Has<MeshRenderer>(first[0]));
    CHECK(!scene.registry.Has<Parent>(first[0]));
    CHECK(scene.registry.TryGet<Name>(second[0])->value == "model Instance");

    // 缓存命中不解析已损坏的源文件，且可直接用资产编号创建。
    { std::ofstream file(modelPath); file << "broken after load"; }
    CHECK(ModelImporter::LoadAsset(scene, modelPath) == asset);
    Scene runtime(scene);
    const auto third = ModelImporter::InstantiateAsset(runtime, asset);
    CHECK(runtime.registry.EntityCount() == 24 && scene.registry.EntityCount() == 16);
    CHECK(runtime.registry.TryGet<Name>(third[0])->value == "model Instance");
    CHECK(runtime.registry.TryGet<Transform>(third[0])->local[3][0] == 0.0f);
    CHECK(runtime.meshes.Revision() == meshRevision);
    bool rejected = false;
    try { ModelImporter::InstantiateAsset(scene, UINT32_MAX); }
    catch (const std::exception&) { rejected = true; }
    CHECK(rejected && scene.registry.EntityCount() == 16);
    { std::ofstream file(modelPath); file << model.dump(2); }

    const auto scenePath = directory / "instances.scene.json";
    SceneSerializer::Save(scene, scenePath);
    Scene loaded = SceneSerializer::Load(scenePath);
    CHECK(loaded.modelAssets.size() == 1);
    CHECK(loaded.meshes.Count() == meshCount && loaded.materials.TextureCount() == textureCount);
    CHECK(loaded.registry.EntityCount() == 16);
    updater.Update(loaded, 0.0f);
    const auto restored = BuildRenderSnapshot(loaded);
    CHECK(restored.drawItems.size() == 8);
    CHECK(restored.drawItems[0].worldTransform == snapshot.drawItems[0].worldTransform);
    CHECK(restored.drawItems[4].worldTransform == snapshot.drawItems[4].worldTransform);
    const auto loadedRevision = loaded.meshes.Revision();
    ModelImporter::Import(loaded, modelPath);
    CHECK(loaded.meshes.Revision() == loadedRevision && loaded.registry.EntityCount() == 24);

    scene.DestroyEntitySubtree(first[0]);
    CHECK(scene.registry.EntityCount() == 8 && scene.registry.IsAlive(second[0]));
    updater.Update(scene, 0.0f);
    CHECK(BuildRenderSnapshot(scene).drawItems.size() == 4);
    CHECK(std::abs(BuildRenderSnapshot(scene).drawItems[0].worldTransform[3][0] - 1.0f) < 0.001f);
    bool allParentsAlive = true;
    scene.registry.ForEach<Parent>([&](Entity, Parent& parent) {
        allParentsAlive = allParentsAlive && scene.registry.IsAlive(parent.entity);
    });
    CHECK(allParentsAlive);
    scene.DestroyEntitySubtree(second[0]);
    CHECK(scene.registry.EntityCount() == 0);
    CHECK(scene.modelAssets.size() == 1 && scene.meshes.Revision() == meshRevision);
    const auto recreated = ModelImporter::InstantiateAsset(scene, asset);
    CHECK(scene.registry.EntityCount() == 8 && recreated.size() == 1);
    CHECK(scene.registry.TryGet<Name>(recreated[0])->value == "model Instance");
    CHECK(scene.meshes.Count() == meshCount && scene.materials.Revision() == materialRevision);
    return true;
}

bool ModelResourceOriginsFollowAssetIdentity()
{
    const auto fixture = std::filesystem::path(TEST_SOURCE_DIR) / "tests/data/model-import-test.gltf";
    const auto directory = std::filesystem::temp_directory_path() / "rebuild-resource-origin-check";
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::error_code error;
            for (const char* name : { "a.gltf", "b.gltf", "origins.scene.json" })
                std::filesystem::remove(directory / name, error);
            std::filesystem::remove(directory, error);
        }
    } cleanup{ directory };
    nlohmann::json model;
    { std::ifstream file(fixture); file >> model; }
    model["meshes"][0]["primitives"].push_back(model["meshes"][0]["primitives"][0]);
    model["meshes"].push_back(model["meshes"][0]);
    const auto aPath = directory / "a.gltf", bPath = directory / "b.gltf";
    { std::ofstream file(aPath); file << model.dump(2); }
    model["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = { 0.1f, 0.9f, 0.2f, 1.0f };
    { std::ofstream file(bPath); file << model.dump(2); }

    Scene scene;
    // 重复加载 A 命中缓存，只保存 A/B 两份资产及来源映射。
    ModelImporter::LoadAsset(scene, aPath);
    ModelImporter::LoadAsset(scene, aPath);
    ModelImporter::LoadAsset(scene, bPath);
    CHECK(scene.modelAssets.size() == 2);
    const std::string aId = scene.modelAssets[0].id, bId = scene.modelAssets[1].id;
    CHECK(!aId.empty() && !bId.empty() && aId != bId);
    CHECK(scene.modelMeshSources.size() == 8);
    CHECK(scene.modelMaterialSources.size() == 2);
    const uint32_t first = MeshLibrary::kBuiltinCount;
    CHECK(scene.modelMeshSources.at(first).assetId == aId);
    CHECK(scene.modelMeshSources.at(first).sourceMesh == 0 && scene.modelMeshSources.at(first).primitive == 0);
    CHECK(scene.modelMeshSources.at(first + 1).sourceMesh == 0 && scene.modelMeshSources.at(first + 1).primitive == 1);
    CHECK(scene.modelMeshSources.at(first + 2).sourceMesh == 1 && scene.modelMeshSources.at(first + 2).primitive == 0);
    CHECK(scene.modelMeshSources.at(first + 3).sourceMesh == 1 && scene.modelMeshSources.at(first + 3).primitive == 1);
    CHECK(scene.modelMaterialSources.at(1).assetId == aId);
    CHECK(scene.modelMaterialSources.at(1).sourceMaterial == 0);
    CHECK(scene.modelMeshSources.at(first + 4).assetId == bId);
    CHECK(scene.modelMaterialSources.at(2).assetId == bId);
    CHECK(scene.materials.GetMaterial(1).baseColorFactor != scene.materials.GetMaterial(2).baseColorFactor);
    CHECK(!scene.modelMeshSources.contains(MeshLibrary::kCube));
    CHECK(!scene.modelMaterialSources.contains(0));
    const auto meshRevision = scene.meshes.Revision();
    std::swap(scene.modelAssets[0], scene.modelAssets[1]);
    CHECK(ModelImporter::LoadAsset(scene, aPath) == 1);
    CHECK(scene.modelAssets[1].id == aId && scene.modelMeshSources.at(first).assetId == aId);
    CHECK(scene.meshes.Revision() == meshRevision);

    const Entity entity = scene.registry.Create();
    scene.registry.Add<Transform>(entity);
    scene.registry.Add<MeshRenderer>(entity, MeshRenderer{ first, 1 });
    const auto path = directory / "origins.scene.json";
    SceneSerializer::Save(scene, path);
    Scene restored = SceneSerializer::Load(path);
    // 表顺序变为 B/A，资源索引变化，但 A 的资产 ID 和实际渲染材质仍正确。
    CHECK(restored.modelAssets.size() == 2);
    CHECK(restored.modelMeshSources.at(first + 4).assetId == aId);
    CHECK(restored.modelMeshSources.at(first).assetId == bId);
    CHECK(restored.modelMaterialSources.at(2).assetId == aId);
    CHECK(BuildRenderSnapshot(restored).drawItems[0].textureIndex == 1);
    CHECK(BuildRenderSnapshot(restored).drawItems[0].tint == BuildRenderSnapshot(scene).drawItems[0].tint);
    Scene clone(scene);
    CHECK(clone.modelAssets[1].id == aId);
    CHECK(clone.modelMeshSources.at(first).assetId == aId);
    clone.modelMeshSources.at(first).assetId = "clone-only-change";
    CHECK(scene.modelMeshSources.at(first).assetId == aId);

    // 恢复了 model-1 的 Scene 中，分配器跳过已占用 ID。
    Scene occupied;
    ModelAsset record;
    record.id = "model-1";
    occupied.modelAssets.push_back(record);
    CHECK(occupied.AllocateModelAssetId() == "model-2");
    CHECK(occupied.AllocateModelAssetId() == "model-3");
    return true;
}

int main()
{
    if (!EntityLifecycleAndComponentStorage() ||
        !StaleHandlesCannotDetachReusedEntityComponents() ||
        !InvalidEntityOperationsAreRejectedOrIgnored() ||
        !ForEachEntityListsOnlyLiveEntitiesAfterIndexReuse() ||
        !EditorCaptureSuppressesCameraAndQuitInput() ||
        !SceneUpdaterProducesRenderSnapshot() ||
        !RegistryCopiesAreIndependent() ||
        !SpritesEnterRenderSnapshotAndSceneRoundTrips() ||
        !GltfModelImportsAndSceneRoundTrips() ||
        !EmissiveMaterialsEnterSnapshotsAndRoundTrip() ||
        !ModelAssetsReuseResourcesAndKeepInstancesIndependent() ||
        !ModelResourceOriginsFollowAssetIdentity() || !Stage6Checks(g_checks) || !Stage7Checks(g_checks) || !Stage8Checks(g_checks) || !Stage9Checks(g_checks) || !Stage10Checks(g_checks)) {
        return EXIT_FAILURE;
    }

    std::cout << "Registry checks passed: " << g_checks << '\n';
    return EXIT_SUCCESS;
}
