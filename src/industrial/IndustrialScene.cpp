#include "industrial/IndustrialScene.h"
#include "scene/Components.h"
#include "scene/ModelImporter.h"
#include "scene/Systems.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

Scene BuildIndustrialScene(const std::filesystem::path& directory)
{
    Scene scene;
    scene.systems = {SceneSystemKind::Industrial, SceneSystemKind::Animation};
    auto& r = scene.registry;
    const auto root = std::filesystem::absolute(directory);
    auto model = [&](const char* file, const std::string& name, glm::vec3 position, glm::vec3 scale) {
        const auto e = ModelImporter::Import(scene, root/"models"/(std::string(file)+".glb")).front();
        r.TryGet<Name>(e)->value = name;
        r.TryGet<Transform>(e)->local = glm::scale(glm::translate(glm::mat4(1),position),scale);
        return e;
    };
    const auto cargoAsset = ModelImporter::LoadAsset(scene,root/"models/craft_cargoA.glb");
    const auto cargoId = scene.modelAssets[cargoAsset].id;
    constexpr float deckY = -0.2f;
    for (int x = -3; x <= 3; ++x)
        for (int z = -2; z <= 2; ++z)
            model("platform_large", "Deck " + std::to_string(x) + ":" + std::to_string(z), { x * 4.0f,deckY,z * 4.0f }, { 2,2,2 });
    std::vector<Entity> stations;
    for (int i = 0; i < 6; ++i) {
        const float angle = float(i) * 6.2831853f / 6;
        const glm::vec3 p{ 9 * std::cos(angle),0,7 * std::sin(angle) };
        const auto station = model("platform_high", "Station " + std::to_string(i), p, { 2,2,2 });
        stations.push_back(station);
        model("hangar_smallA", "Assembly bay " + std::to_string(i), p + glm::vec3(0, 0, -2.6f), { 1.8f,1.8f,1.8f });
        model("machine_generatorLarge", "Processor " + std::to_string(i), p + glm::vec3(2, 0, 0), { 2,2,2 });
        const auto rotor = model("pipe_ring", "Rotor " + std::to_string(i), p + glm::vec3(2, 1.4f, 0), { 1.1f,1.1f,1.1f });
        r.Add<MachineMotion>(rotor, MachineMotion{ 0,{0,1,0},1,1.2f,float(i) });
        const auto piston = model("machine_barrel", "Press head " + std::to_string(i), p + glm::vec3(-1.7f, 1, 0), { 1.5f,1.5f,1.5f });
        r.Add<MachineMotion>(piston, MachineMotion{ 1,{0,1,0},0.45f,2.5f,float(i) });

        const auto base = model("machine_barrelLarge", "Arm base " + std::to_string(i), p + glm::vec3(-0.8f, 0, 1.5f), { 1,1,1 });
        const auto joint = r.Create();
        r.Add<Name>(joint,Name{"Pick/place joint "+std::to_string(i)});
        r.Add<Transform>(joint, Transform{ glm::translate(glm::mat4(1),{0,1.2f,0}) });
        r.Add<Parent>(joint,Parent{base});
        r.Add<MachineMotion>(joint,MachineMotion{2,{0,1,0},1.1f,0.9f,float(i)});
        const auto link = model("structure","Arm link "+std::to_string(i),{0,0,0},{0.24f,1.6f,0.24f});
        r.TryGet<Transform>(link)->local = glm::rotate(glm::mat4(1),1.5707963f,glm::vec3(1,0,0))*r.TryGet<Transform>(link)->local;
        r.Add<Parent>(link,Parent{joint});
        const auto grip = model("barrel","Pick/place workpiece "+std::to_string(i),{0,1.2f,1.6f},{1.5f,1.5f,1.5f});
        r.Add<Parent>(grip,Parent{base});
        r.Add<MachineMotion>(grip,MachineMotion{3,{0,1,0},1.1f,0.9f,float(i)});
        for (int endpoint : {-1,1}) {
            const auto offset = glm::vec3(glm::rotate(glm::mat4(1),float(endpoint)*1.1f,glm::vec3(0,1,0))*glm::vec4(0,0,1.6f,0));
            model("platform_high",std::string(endpoint<0?"Pickup socket ":"Place socket ")+std::to_string(i),
                p+glm::vec3(-0.8f,0,1.5f)+offset,{0.7f,1.2f,0.7f});
        }
    }
    for (uint32_t i = 0; i < stations.size(); ++i)
        r.Add<CargoRoute>(stations[i], CargoRoute{ stations[(i + 3) % stations.size()],cargoId,
            64,3.0f,0.15f,0.15f,3.2f + float(i) * 0.45f });
    model("satelliteDish_detailed","Dispatch antenna",{0,0,0},{3,3,3});
    model("chimney_detailed","Energy tower",{0,0,-5},{3,4,3});
    scene.environment.sourcePath = (root/"kloppenheim_06_puresky_1k.hdr").generic_string();
    scene.environment.enabled = scene.environment.background = true;
    scene.environment.intensity = 0.65f;
    scene.environmentData = LoadEnvironment(root/"kloppenheim_06_puresky_1k.hdr").data;
    scene.camera.position = {22,17,26};
    const auto direction = glm::normalize(glm::vec3(0,1,0)-scene.camera.position);
    scene.camera.yaw = std::atan2(direction.z,direction.x);
    scene.camera.pitch = std::asin(direction.y);
    scene.camera.farPlane = 160;
    scene.camera.fovDegrees = 44;
    TransformSystem(r);
    return scene;
}
