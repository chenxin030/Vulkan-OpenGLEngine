#include "SceneSerializer.h"

#include "Scene.h"
#include "scene/Components.h"
#include "scene/ModelImporter.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <array>
#include <limits>
#include <unordered_set>

namespace {
using Json = nlohmann::json;
constexpr int kSceneVersion = 3; // Save/Load 只支持当前格式，不保留旧版本迁移分支。

std::string PathUtf8(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

uint32_t ReadIndex(const Json& value)
{
    if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
        (value.is_number_integer() && !value.is_number_unsigned() && value.get<int64_t>() < 0) ||
        value.get<uint64_t>() > UINT32_MAX)
        throw std::runtime_error("resource/entity index must be an unsigned 32-bit integer");
    return value.get<uint32_t>();
}

constexpr std::array<const char*, MeshLibrary::kBuiltinCount> kBuiltinNames{
    "cube", "pyramid", "plane", "cylinder", "quad"
};

const ModelAsset& AssetById(const Scene& scene, const std::string& id)
{
    for (const auto& asset : scene.modelAssets) if (asset.id == id) return asset;
    throw std::runtime_error("unknown model asset ID: " + id);
}

uint32_t ResolveMesh(const Scene& scene, const Json& ref)
{
    if (!ref.is_object()) throw std::runtime_error("mesh reference must be an object");
    if (ref.contains("builtin")) {
        if (ref.contains("asset")) throw std::runtime_error("ambiguous mesh reference");
        const auto name = ref.at("builtin").get<std::string>();
        for (uint32_t i = 0; i < kBuiltinNames.size(); ++i) if (name == kBuiltinNames[i]) return i;
        throw std::runtime_error("unknown builtin mesh: " + name);
    }
    const auto& asset = AssetById(scene, ref.at("asset").get<std::string>());
    const uint32_t sourceMesh = ReadIndex(ref.at("sourceMesh"));
    const uint32_t primitive = ReadIndex(ref.at("primitive"));
    for (uint32_t index : asset.meshIndices) {
        const auto& source = scene.modelMeshSources.at(index);
        if (source.sourceMesh == sourceMesh && source.primitive == primitive) return index;
    }
    throw std::runtime_error("model mesh/primitive reference does not exist: " + asset.id);
}

uint32_t ResolveMaterial(const Scene& scene, const Json& ref)
{
    if (!ref.is_object()) throw std::runtime_error("material reference must be an object");
    if (ref.contains("builtin")) {
        if (ref.contains("asset") || ref.at("builtin") != "default")
            throw std::runtime_error("invalid builtin material reference");
        return 0;
    }
    const auto& asset = AssetById(scene, ref.at("asset").get<std::string>());
    int32_t key = -1;
    if (ref.contains("default")) {
        if (ref.at("default") != true || ref.contains("sourceMaterial"))
            throw std::runtime_error("invalid default material reference");
    } else {
        const auto source = ReadIndex(ref.at("sourceMaterial"));
        if (source > INT32_MAX) throw std::runtime_error("source material index is too large");
        key = static_cast<int32_t>(source);
    }
    for (uint32_t index : asset.materialIndices)
        if (scene.modelMaterialSources.at(index).sourceMaterial == key) return index;
    throw std::runtime_error("model material reference does not exist: " + asset.id);
}

Json MeshReference(const Scene& scene, uint32_t index)
{
    if (index >= scene.meshes.Count()) throw std::runtime_error("cannot save invalid mesh index");
    if (index < kBuiltinNames.size()) return {{"builtin", kBuiltinNames[index]}};
    const auto found = scene.modelMeshSources.find(index);
    if (found == scene.modelMeshSources.end()) throw std::runtime_error("cannot save mesh without asset provenance");
    const auto& source = found->second;
    Json ref{{"asset", source.assetId}, {"sourceMesh", source.sourceMesh}, {"primitive", source.primitive}};
    ResolveMesh(scene, ref); // 写文件前检查该来源确实能解析到已加载资源。
    return ref;
}

Json MaterialReference(const Scene& scene, uint32_t index)
{
    if (index >= scene.materials.MaterialCount()) throw std::runtime_error("cannot save invalid material index");
    if (index == 0) return {{"builtin", "default"}};
    const auto found = scene.modelMaterialSources.find(index);
    if (found == scene.modelMaterialSources.end()) throw std::runtime_error("cannot save material without asset provenance");
    const auto& source = found->second;
    Json ref{{"asset", source.assetId}};
    if (source.sourceMaterial == -1) ref["default"] = true;
    else ref["sourceMaterial"] = source.sourceMaterial;
    ResolveMaterial(scene, ref);
    return ref;
}

std::filesystem::path PathFromUtf8(const std::string& input)
{
    std::u8string value;
    value.reserve(input.size());
    for (unsigned char byte : input) value.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(value);
}

Json Vec3(const glm::vec3& value)
{
    return Json::array({ value.x, value.y, value.z });
}

glm::vec3 ReadVec3(const Json& value, const char* field)
{
    if (!value.is_array() || value.size() != 3 || !value[0].is_number() ||
        !value[1].is_number() || !value[2].is_number()) {
        throw std::runtime_error(std::string("invalid vec3 field: ") + field);
    }
    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

Json Matrix(const glm::mat4& matrix)
{
    Json values = Json::array();
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            values.push_back(matrix[column][row]);
    return values;
}

glm::mat4 ReadMatrix(const Json& value)
{
    if (!value.is_array() || value.size() != 16)
        throw std::runtime_error("invalid Transform.local matrix");
    glm::mat4 matrix(1.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const Json& element = value[column * 4 + row];
            if (!element.is_number()) throw std::runtime_error("non-numeric Transform.local value");
            matrix[column][row] = element.get<float>();
        }
    }
    return matrix;
}
}

void SceneSerializer::Save(const Scene& scene, const std::filesystem::path& path)
{
    Json root;
    root["version"] = kSceneVersion;
    ValidateEnvironmentSettings(scene.environment);
    if (!scene.environment.sourcePath.empty()) {
        const auto source = std::filesystem::absolute(PathFromUtf8(scene.environment.sourcePath));
        const auto directory = std::filesystem::absolute(path).parent_path();
        std::error_code error;
        auto relative = std::filesystem::relative(source, directory, error);
        root["environment"] = {{"path", PathUtf8(error ? source : relative)},
            {"enabled", scene.environment.enabled}, {"background", scene.environment.background},
            {"intensity", scene.environment.intensity}};
    }
    root["camera"] = {
        {"position", Vec3(scene.camera.position)}, {"yaw", scene.camera.yaw},
        {"pitch", scene.camera.pitch}, {"fovDegrees", scene.camera.fovDegrees},
        {"nearPlane", scene.camera.nearPlane}, {"farPlane", scene.camera.farPlane},
        {"projection", scene.camera.projection == CameraProjection::Orthographic ? "orthographic" : "perspective"},
        {"orthographicHeight", scene.camera.orthographicHeight}
    };
    root["entities"] = Json::array();
    root["modelAssets"] = Json::array();
    std::unordered_set<std::string> savedIds;
    for (const ModelAsset& asset : scene.modelAssets) {
        if (asset.id.empty() || asset.id.size() > 128 || asset.id.find('\0') != std::string::npos ||
            !savedIds.insert(asset.id).second ||
            !asset.fingerprint.starts_with("sha256-import-v1:") || asset.fingerprint.size() != 81)
            throw std::runtime_error("invalid or duplicate model asset identity/fingerprint");
        std::filesystem::path modelPath = PathFromUtf8(asset.sourcePath);
        if (modelPath.is_relative()) modelPath = std::filesystem::absolute(modelPath);
        std::filesystem::path sceneDirectory = path.has_parent_path()
            ? std::filesystem::absolute(path.parent_path())
            : std::filesystem::current_path();
        std::error_code error;
        if (modelPath.root_name() != sceneDirectory.root_name()) {
            root["modelAssets"].push_back({{"id", asset.id}, {"path", PathUtf8(modelPath)}, {"fingerprint", asset.fingerprint}});
        } else {
            std::filesystem::path relative = std::filesystem::relative(modelPath, sceneDirectory, error);
            root["modelAssets"].push_back({{"id", asset.id},
                {"path", PathUtf8(error ? modelPath : relative)}, {"fingerprint", asset.fingerprint}});
        }
    }
    scene.registry.ForEachEntity([&](Entity entity) {
        Json item;
        item["id"] = entity.index;
        if (const Name* name = scene.registry.TryGet<Name>(entity)) item["name"] = name->value;
        if (const Parent* parent = scene.registry.TryGet<Parent>(entity)) {
            if (!scene.registry.IsAlive(parent->entity)) throw std::runtime_error("cannot save dead parent");
            item["parent"] = parent->entity.index;
        }
        if (const Transform* transform = scene.registry.TryGet<Transform>(entity))
            item["transform"] = Matrix(transform->local);
        if (const MeshRenderer* mesh = scene.registry.TryGet<MeshRenderer>(entity)) {
            item["mesh"] = MeshReference(scene, mesh->meshIndex);
            item["material"] = MaterialReference(scene, mesh->materialIndex);
        }
        if (const SpriteRenderer* sprite = scene.registry.TryGet<SpriteRenderer>(entity))
            item["sprite"] = {{"tint", Vec3(sprite->tint)}};
        if (const Spin* spin = scene.registry.TryGet<Spin>(entity))
            item["spin"] = {{"axis", Vec3(spin->axis)}, {"speed", spin->speed}, {"angle", spin->angle}};
        if (const Bob* bob = scene.registry.TryGet<Bob>(entity))
            item["bob"] = {{"amplitude", bob->amplitude}, {"speed", bob->speed}, {"phase", bob->phase}};
        root["entities"].push_back(std::move(item));
    });

    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("cannot open scene for writing: " + path.string());
    file << root.dump(2) << '\n';
    if (!file) throw std::runtime_error("failed writing scene: " + path.string());
}

Scene SceneSerializer::Load(const std::filesystem::path& path)
{
    // Load 返回一份完整的新 Scene，不直接修改编辑器当前场景。
    // 文件、JSON、资源或层级校验失败时向外抛异常；Editor 只有在本函数成功返回后才替换 editScene。
    // binary 只表示按原始字节读取文件，文件内容仍是 JSON 文本。
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open scene: " + path.string());
    Json root;
    file >> root;
    // 先检查整体格式；后面 at/get 会继续检查必需字段是否存在、类型是否正确。
    // 当前只接受 v3：资产身份 + 源子资源引用。保留 version 字段，以免把其他格式误读为当前格式。
    if (!root.is_object() || root.value("version", 0) != kSceneVersion)
        throw std::runtime_error("unsupported scene version; only version 3 is supported");
    if (!root.contains("entities") ||
        !root["entities"].is_array() || !root.contains("camera"))
        throw std::runtime_error("unsupported or malformed scene file");

    // 第一阶段：准备资源库和模型模板。Scene 构造时已有内置网格及引擎默认材质。
    // 此处不调用 Import/InstantiateAsset；实例 Entity 必须按下面的 entities 数据恢复，避免重复创建。
    Scene scene;
    // v3 可选字段；旧 v3 场景没有环境时保持关闭。源内容变化允许重新烘焙。
    if (root.contains("environment")) {
        const auto& env = root.at("environment");
        scene.environment.sourcePath = env.at("path").get<std::string>();
        scene.environment.enabled = env.at("enabled").get<bool>();
        scene.environment.background = env.at("background").get<bool>();
        scene.environment.intensity = env.at("intensity").get<float>();
        ValidateEnvironmentSettings(scene.environment);
        if (scene.environment.sourcePath.empty()) throw std::runtime_error("empty environment path");
        auto source = PathFromUtf8(scene.environment.sourcePath);
        if (source.is_relative()) source = std::filesystem::absolute(path).parent_path() / source;
        source = std::filesystem::absolute(source).lexically_normal();
        scene.environmentData = LoadEnvironment(source).data;
        scene.environment.sourcePath = PathUtf8(source);
    }
    if (!root.contains("modelAssets") || !root.at("modelAssets").is_array())
        throw std::runtime_error("scene modelAssets must be an array");
    std::unordered_set<std::string> ids; // 检查同一场景文件内的资产 ID 是否重复。
    for (const Json& asset : root.at("modelAssets")) {
        // 每项包含 path/id/fingerprint
        const auto storedPath = asset.at("path").get<std::string>();
        std::filesystem::path modelPath = PathFromUtf8(storedPath);
        // 模型相对路径以场景文件所在目录为基准，而不是以可执行程序所在目录为基准。
        // 若传入场景路径只有文件名，则其所在目录就是当前工作目录。
        if (modelPath.is_relative() && path.has_parent_path()) modelPath = path.parent_path() / modelPath;
        modelPath = std::filesystem::absolute(modelPath).lexically_normal();
        const auto id = asset.at("id").get<std::string>();
        const auto fingerprint = asset.at("fingerprint").get<std::string>();
        // insert 返回 {迭代器, 是否插入成功}，second=false 表示 ID 已出现过。
        // 指纹这里只检查版本前缀及总长度；实际源内容是否匹配由 LoadAsset 检查。
        if (id.empty() || id.size() > 128 || id.find('\0') != std::string::npos || !ids.insert(id).second ||
            !fingerprint.starts_with("sha256-import-v1:") || fingerprint.size() != 81)
            throw std::runtime_error("invalid or duplicate asset ID/fingerprint");
        // 保留保存时的资产 ID，重新读取源文件/依赖并校验导入基线，建立当前资源索引及来源映射。
        ModelImporter::LoadAsset(scene, modelPath, id, fingerprint);
    }
    // 第二阶段：恢复相机参数，重新打开后继续使用保存的视点和投影方式。
    const Json& camera = root.at("camera");
    scene.camera.position = ReadVec3(camera.at("position"), "camera.position");
    scene.camera.yaw = camera.at("yaw").get<float>();
    scene.camera.pitch = camera.at("pitch").get<float>();
    scene.camera.fovDegrees = camera.at("fovDegrees").get<float>();
    scene.camera.nearPlane = camera.at("nearPlane").get<float>();
    scene.camera.farPlane = camera.at("farPlane").get<float>();
    scene.camera.orthographicHeight = camera.at("orthographicHeight").get<float>();
    const std::string projection = camera.at("projection").get<std::string>();
    if (projection == "orthographic") scene.camera.projection = CameraProjection::Orthographic;
    else if (projection == "perspective") scene.camera.projection = CameraProjection::Perspective;
    else throw std::runtime_error("invalid camera projection");

    // 第三阶段：恢复 Entity 及 Component 。磁盘 ID 只是文件内关联键，不是可以直接复用的 Entity 句柄。
    std::unordered_map<uint32_t, Entity> entityMap; // 文件中的 Entity  ID → 新 Registry 创建的 Entity。
    std::vector<std::pair<Entity, uint32_t>> pendingParents; // 新子 Entity  + 文件中的父 ID，稍后统一关联。
    for (const Json& item : root.at("entities")) {
        if (!item.is_object()) throw std::runtime_error("scene entity must be an object");
        const Entity entity = scene.registry.Create();
        // 读取显式 ID，并拒绝重复；新 Registry 的句柄通过 entityMap 与它关联。
        const uint32_t savedId = ReadIndex(item.at("id"));
        if (!entityMap.emplace(savedId, entity).second) throw std::runtime_error("duplicate entity id in scene");
        if (item.contains("name")) scene.registry.Add<Name>(entity, Name{ item.at("name").get<std::string>() });
        // 父 Entity 可能排在子 Entity 后面，所以现在只记录父 ID，不马上查找 Entity。
        if (item.contains("parent")) pendingParents.emplace_back(entity, ReadIndex(item.at("parent")));
        if (item.contains("transform")) {
            Transform transform;
            transform.local = ReadMatrix(item.at("transform"));
            transform.world = transform.local;
            scene.registry.Add<Transform>(entity, transform);
        }
        if (item.contains("mesh")) {
            // 内置名称或 {asset ID, 源子资源标识} → 本次加载的运行时资源索引。
            // 因此资产表加载顺序改变时，索引可以变化，但引用的资源含义保持不变。
            const uint32_t index = ResolveMesh(scene, item.at("mesh"));
            const uint32_t material = ResolveMaterial(scene, item.at("material"));
            // 文件中的稳定引用只在这里解析，渲染端继续使用 MeshRenderer 的运行时索引。
            if (index >= scene.meshes.Count()) throw std::runtime_error("scene mesh index is out of range");
            if (material >= scene.materials.MaterialCount()) throw std::runtime_error("scene material index is out of range");
            scene.registry.Add<MeshRenderer>(entity, MeshRenderer{ index, material });
        }
        // 按文件中实际存在的字段添加 Component ，不要求每个 Entity 都具备所有 Component 。
        if (item.contains("sprite")) {
            SpriteRenderer sprite;
            sprite.tint = ReadVec3(item.at("sprite").at("tint"), "sprite.tint");
            scene.registry.Add<SpriteRenderer>(entity, sprite);
        }
        // 动画 Component 同时恢复参数与已保存的 angle/phase，让系统能够继续更新。
        if (item.contains("spin")) {
            const Json& data = item.at("spin");
            scene.registry.Add<Spin>(entity, Spin{ ReadVec3(data.at("axis"), "spin.axis"),
                data.at("speed").get<float>(), data.at("angle").get<float>() });
        }
        if (item.contains("bob")) {
            const Json& data = item.at("bob");
            scene.registry.Add<Bob>(entity, Bob{ data.at("amplitude").get<float>(),
                data.at("speed").get<float>(), data.at("phase").get<float>() });
        }
    }
    // 第四阶段：所有 Entity 已经创建，利用 ID 映射把文件中的父子关系转换成新 Entity 句柄。
    for (const auto& [entity, savedParent] : pendingParents) {
        const auto found = entityMap.find(savedParent);
        if (found == entityMap.end()) throw std::runtime_error("scene parent id does not exist");
        // Parent 用于变换继承，父子双方都必须有 Transform。
        if (!scene.registry.Has<Transform>(entity) || !scene.registry.Has<Transform>(found->second))
            throw std::runtime_error("scene parent and child must have Transform components");
        scene.registry.Add<Parent>(entity, Parent{ found->second });
    }
    // 第五阶段：沿每个子 Entity 的祖先链检查环，包括“自己是自己的父节点”。
    // seen 每条祖先链单独创建；同一条链再次遇到同一个 Entity ，说明层级形成了环。
    for (const auto& [entity, ignored] : pendingParents) {
        (void)ignored;
        std::unordered_set<uint32_t> seen;
        Entity current = entity;
        while (const Parent* parent = scene.registry.TryGet<Parent>(current)) {
            if (!seen.insert(current.index).second) throw std::runtime_error("scene hierarchy contains a cycle");
            current = parent->entity;
        }
    }
    // 资源、 Entity 和层级均恢复并通过检查后，才把完整场景交给调用者。
    return scene;
}
