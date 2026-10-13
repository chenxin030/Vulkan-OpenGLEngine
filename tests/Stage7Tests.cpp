#include "editor/EditorGeometry.h"
#include "scene/Components.h"
#include "scene/ModelImporter.h"
#include "scene/SceneSerializer.h"
#include "scene/SceneUpdater.h"
#include <iostream>
#include <limits>
#include <filesystem>

namespace {
bool Close(const glm::mat4& a, const glm::mat4& b)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > 1e-3f) return false;
    return true;
}
glm::vec2 Screen(const Scene& scene, glm::vec3 p, glm::vec2 size, float aspect)
{
    const auto h = EditorGeometry::Projection(scene.camera, aspect) * scene.camera.ViewMatrix() * glm::vec4(p, 1);
    return { (h.x/h.w + 1)*size.x/2, (1-h.y/h.w)*size.y/2 };
}
Entity Quad(Scene& scene, glm::vec3 position)
{
    Entity e = scene.registry.Create();
    Transform t;
    t.local = glm::translate(glm::mat4(1), position);
    scene.registry.Add<Transform>(e, t);
    scene.registry.Add<SpriteRenderer>(e);
    return e;
}
}

bool Stage7Checks(int& assertions)
{
#define VERIFY(...) do { ++assertions; if (!(__VA_ARGS__)) { \
    std::cerr << "Stage7 check failed at " << __LINE__ << ": " #__VA_ARGS__ << '\n'; return false; } } while(false)
    using namespace EditorGeometry;
    const Trs ordered{ {1,2,3}, {25,-35,40}, {1.2f,0.8f,1.4f} };
    const auto explicitOrder = glm::translate(glm::mat4(1),ordered.position) *
        glm::rotate(glm::mat4(1),glm::radians(40.0f),glm::vec3(0,0,1)) *
        glm::rotate(glm::mat4(1),glm::radians(-35.0f),glm::vec3(0,1,0)) *
        glm::rotate(glm::mat4(1),glm::radians(25.0f),glm::vec3(1,0,0)) *
        glm::scale(glm::mat4(1),ordered.scale);
    VERIFY(Close(Compose(ordered), explicitOrder));
    for (float pitch : {90.0f, -90.0f, 89.999f, -89.999f, 91.0f, -91.0f}) {
        auto matrix = Compose({ {}, {25,pitch,40}, {1.2f,0.8f,1.4f} });
        auto value = Decompose(matrix);
        if (!value) std::cerr << "TRS decomposition failed for pitch " << pitch << '\n';
        VERIFY(value.has_value());
        VERIFY(Close(Compose(*value),matrix));
    }
    // 多种姿态、非均匀缩放，以及只改位置时完整保留三维旋转/缩放。
    for (glm::vec3 rotation : { glm::vec3(23, 42, -67), glm::vec3(0, 0, 135), glm::vec3(-78, 20, 82) }) {
        Trs value{ {2,-3,4}, rotation, {2,3,4} };
        auto matrix = Compose(value);
        auto decoded = Decompose(matrix);
        VERIFY(decoded.has_value());
        VERIFY(Close(Compose(*decoded), matrix));
        decoded->position.x += 5;
        const auto moved = Compose(*decoded);
        for (int c=0; c<3; ++c) VERIFY(glm::length(glm::vec3(moved[c]-matrix[c])) < 1e-3f);
        VERIFY(std::abs(moved[3].x-matrix[3].x-5) < 1e-3f);
    }
    auto bad = glm::mat4(1); bad[1][0] = 0.3f;
    VERIFY(!Decompose(bad));
    VERIFY(bad[1][0] == 0.3f);
    bad = glm::mat4(1); bad[0][0] = -1;
    VERIFY(!Decompose(bad));
    bad[0][0] = 0;
    VERIFY(!Decompose(bad));
    bad[0][0] = std::numeric_limits<float>::infinity();
    VERIFY(!Decompose(bad));
    bad = glm::mat4(1); bad[0][3] = 0.1f;
    VERIFY(!Decompose(bad));

    Scene scene = BuildDemo2DScene();
    SceneUpdater updater;
    updater.Update(scene, 0);
    const glm::vec2 size(1200, 800);
    for (int i=0; i<3; ++i) {
        const glm::vec3 p((i-1)*2, 0, 0);
        Entity hit = Pick(scene, Screen(scene,p,size,1.5f), size,1.5f);
        VERIFY(hit.IsValid());
        VERIFY(hit.index == static_cast<uint32_t>(i));
        // 高 DPI 同比变化的 framebuffer 不改变逻辑坐标，非同比变化需使用像素宽高比。
        VERIFY(Pick(scene, Screen(scene,p,size,2.0f), size,2.0f) == hit);
    }
    VERIFY(!Pick(scene,{0,0},size,1.5f).IsValid());
    VERIFY(!Pick(scene,{-1,400},size,1.5f).IsValid());
    VERIFY(!Pick(scene,{600,400},{0,800},1.5f).IsValid());
    VERIFY(!Pick(scene,{600,400},size,0).IsValid());
    VERIFY(!Pick(scene,{std::numeric_limits<float>::quiet_NaN(),400},size,1.5f).IsValid());
    // 正交/透视均选择最近前表面，不选择背面、近裁面之前、远裁面之后的几何。
    for (auto projection : {CameraProjection::Orthographic, CameraProjection::Perspective}) {
        Scene overlap;
        overlap.camera = scene.camera;
        overlap.camera.projection = projection;
        const auto back = Quad(overlap,{0,0,0});
        const auto front = Quad(overlap,{0,0,2});
        updater.Update(overlap,0);
        VERIFY(Pick(overlap,{600,400},size,1.5f) == front);
        overlap.registry.TryGet<Transform>(front)->local[0][0] = -1;
        updater.Update(overlap,0);
        VERIFY(Pick(overlap,{600,400},size,1.5f) == back);
        overlap.registry.TryGet<Transform>(front)->local = glm::translate(glm::mat4(1),glm::vec3(0,0,9.95f));
        updater.Update(overlap,0);
        VERIFY(Pick(overlap,{600,400},size,1.5f) == back);
        overlap.registry.TryGet<Transform>(front)->local[3][2] = -200;
        updater.Update(overlap,0);
        VERIFY(Pick(overlap,{600,400},size,1.5f) == back);
        overlap.registry.Destroy(front);
        VERIFY(!RootEntity(overlap, front).IsValid());
    }
    // 多 primitive / 层级根映射与死父/环安全。
    const auto root = scene.registry.Create(); scene.registry.Add<Transform>(root);
    scene.registry.Add<Parent>(Entity{1,0}, Parent{root});
    updater.Update(scene,0);
    VERIFY(Pick(scene,{600,400},size,1.5f) == root);
    scene.registry.Add<Parent>(root, Parent{Entity{1,0}});
    VERIFY(!RootEntity(scene,Entity{1,0}).IsValid());
    scene.registry.Remove<Parent>(root);
    scene.registry.Destroy(root);
    VERIFY(!RootEntity(scene,Entity{1,0}).IsValid());

    // 真正的模型实例：根变换传播到全部孩子，模板/另一实例/运行副本互不影响，v3 往返。
    Scene models;
    models.camera = BuildDemo2DScene().camera;
    auto first = ModelImporter::Import(models, std::filesystem::path(TEST_SOURCE_DIR)/"tests/data/model-import-test.gltf").front();
    auto second = ModelImporter::InstantiateAsset(models,0).front();
    const auto source = models.modelAssets[0].nodes[0].localTransform;
    const auto editMatrix = Compose({ {1,2,-3}, {25,-40,30}, {1.2f,0.8f,1.5f} });
    models.registry.TryGet<Transform>(first)->local = editMatrix;
    updater.Update(models,0);
    VERIFY(Close(models.registry.TryGet<Transform>(first)->world,editMatrix));
    VERIFY(Close(models.registry.TryGet<Transform>(second)->local,glm::mat4(1)));
    VERIFY(Close(models.modelAssets[0].nodes[0].localTransform,source));
    Entity primitive;
    models.registry.ForEachEntity([&](Entity e) {
        if (models.registry.Has<MeshRenderer>(e) && RootEntity(models,e) == first) primitive = e;
    });
    VERIFY(primitive.IsValid());
    const auto& mesh = models.meshes.Get(models.registry.TryGet<MeshRenderer>(primitive)->meshIndex);
    const auto& world = models.registry.TryGet<Transform>(primitive)->world;
    const auto centroid = (mesh.vertices[mesh.indices[0]].position + mesh.vertices[mesh.indices[1]].position +
        mesh.vertices[mesh.indices[2]].position) / 3.0f;
    VERIFY(Pick(models,Screen(models,glm::vec3(world*glm::vec4(centroid,1)),size,1.5f),size,1.5f) == first);
    models.registry.ForEachEntity([&](Entity e) {
        if (RootEntity(models,e) == first && e != first) {
            const auto* parent = models.registry.TryGet<Parent>(e);
            const auto* t = models.registry.TryGet<Transform>(e);
            if (!Close(t->world, models.registry.TryGet<Transform>(parent->entity)->world*t->local))
                throw std::runtime_error("root TRS did not propagate to child");
        }
    });
    Scene runtime(models);
    runtime.registry.TryGet<Transform>(first)->local = glm::mat4(1);
    VERIFY(Close(models.registry.TryGet<Transform>(first)->local,editMatrix));
    const auto path = std::filesystem::temp_directory_path()/"rebuild-stage7-roundtrip.scene.json";
    SceneSerializer::Save(models,path);
    auto restored = SceneSerializer::Load(path);
    std::filesystem::remove(path);
    bool restoredMatrix = false;
    restored.registry.ForEachEntity([&](Entity e) {
        if (const auto* t = restored.registry.TryGet<Transform>(e)) restoredMatrix |= Close(t->local,editMatrix);
    });
    VERIFY(restoredMatrix);
    return true;
#undef VERIFY
}
