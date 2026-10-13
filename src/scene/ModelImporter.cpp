#include "ModelImporter.h"

#include "Scene.h"
#include "scene/Components.h"
#include "scene/MeshLibrary.h"
#include "scene/ContentFingerprint.h"

#include <tiny_gltf_v3.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kNoTexture = 0xFFFFFFFFu;
constexpr uint64_t kMaxModelFileSize = 128ull * 1024ull * 1024ull;

struct ModelDeleter
{
    void operator()(tg3_model* model) const { tg3_model_free(model); }
};

struct ErrorStack
{
    tg3_error_stack value{};
    ErrorStack() { tg3_error_stack_init(&value); }
    ~ErrorStack() { tg3_error_stack_free(&value); }
};

struct FileContext
{
    std::filesystem::path root;
    ContentFingerprint* fingerprint = nullptr;
};

std::string PathUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::filesystem::path PathFromUtf8(const char* data, size_t size)
{
    std::u8string value;
    value.reserve(size);
    for (size_t i = 0; i < size; ++i) value.push_back(static_cast<char8_t>(data[i]));
    return std::filesystem::path(value);
}

bool IsWithinRoot(const std::filesystem::path& candidate, const std::filesystem::path& root)
{
    std::error_code error;
    const auto full = std::filesystem::weakly_canonical(candidate, error);
    if (error) return false;
    const auto base = std::filesystem::weakly_canonical(root, error);
    if (error) return false;
    auto relative = full.lexically_relative(base);
    if (relative.empty()) return full == base;
    const auto first = *relative.begin();
    return first != ".." && !relative.is_absolute();
}

int32_t ReadExternalFile(uint8_t** outData, uint64_t* outSize,
    const char* path, uint32_t pathLength, void* userData)
{
    auto& context = *static_cast<FileContext*>(userData);
    const std::filesystem::path candidate = PathFromUtf8(path, pathLength);
    if (!IsWithinRoot(candidate, context.root)) return 0;

    std::ifstream file(candidate, std::ios::binary | std::ios::ate);
    if (!file) return 0;
    const auto end = file.tellg();
    if (end < 0 || static_cast<uint64_t>(end) > kMaxModelFileSize) return 0;
    const size_t size = static_cast<size_t>(end);
    auto* data = static_cast<uint8_t*>(std::malloc(std::max<size_t>(size, 1)));
    if (!data) return 0;
    file.seekg(0);
    if (size != 0) file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!file && size != 0) { std::free(data); return 0; }
    *outData = data;
    *outSize = size;
    if (context.fingerprint) context.fingerprint->AddBlock(2, { data, size });
    return 1;
}

void FreeExternalFile(uint8_t* data, uint64_t, void*)
{
    std::free(data);
}

std::vector<uint8_t> DecodeDataUri(tg3_str uri)
{
    const std::string value(uri.data, uri.len);
    const size_t comma = value.find(',');
    if (comma == std::string::npos || value.substr(0, comma).find(";base64") == std::string::npos)
        throw std::runtime_error("only base64 glTF image data URIs are supported");
    std::vector<uint8_t> output;
    uint32_t accumulator = 0;
    int bits = 0;
    for (size_t i = comma + 1; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch == '=') break;
        int digit = ch >= 'A' && ch <= 'Z' ? ch - 'A' :
            ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 :
            ch >= '0' && ch <= '9' ? ch - '0' + 52 : ch == '+' ? 62 : ch == '/' ? 63 : -1;
        if (digit < 0) throw std::runtime_error("invalid base64 image data URI");
        accumulator = (accumulator << 6) | static_cast<uint32_t>(digit);
        bits += 6;
        if (bits >= 8) { bits -= 8; output.push_back(static_cast<uint8_t>(accumulator >> bits)); }
    }
    return output;
}

std::vector<uint8_t> ReadImageBytes(const tg3_model& model, const tg3_image& image,
    const std::filesystem::path& root)
{
    if (image.uri.data && image.uri.len) {
        if (tg3_is_data_uri(image.uri.data, image.uri.len)) return DecodeDataUri(image.uri);
        const std::filesystem::path candidate = root / PathFromUtf8(image.uri.data, image.uri.len);
        if (!IsWithinRoot(candidate, root)) throw std::runtime_error("glTF image path escapes model directory");
        std::ifstream file(candidate, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0 || static_cast<uint64_t>(file.tellg()) > kMaxModelFileSize)
            throw std::runtime_error("cannot read glTF image: " + candidate.string());
        std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) throw std::runtime_error("failed reading glTF image");
        return bytes;
    }
    if (image.buffer_view < 0 || static_cast<uint32_t>(image.buffer_view) >= model.buffer_views_count)
        throw std::runtime_error("glTF image has neither URI nor valid bufferView");
    const tg3_buffer_view& view = model.buffer_views[image.buffer_view];
    if (view.buffer < 0 || static_cast<uint32_t>(view.buffer) >= model.buffers_count)
        throw std::runtime_error("glTF image bufferView references an invalid buffer");
    const tg3_buffer& buffer = model.buffers[view.buffer];
    if (view.byte_offset > buffer.data.count || view.byte_length > buffer.data.count - view.byte_offset)
        throw std::runtime_error("glTF image bufferView range is invalid");
    return std::vector<uint8_t>(buffer.data.data + view.byte_offset,
        buffer.data.data + view.byte_offset + view.byte_length);
}

Image DecodeModelImage(const tg3_model& model, const tg3_image& source,
    const std::filesystem::path& root, ContentFingerprint& fingerprint)
{
    const std::vector<uint8_t> bytes = ReadImageBytes(model, source, root);
    fingerprint.AddBlock(3, bytes);
    if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("glTF image is empty or too large");
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
        &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        throw std::runtime_error("failed decoding glTF base-color image");
    }
    Image decoded;
    decoded.width = static_cast<uint32_t>(width);
    decoded.height = static_cast<uint32_t>(height);
    decoded.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return decoded;
}

std::string ParserErrors(const tg3_error_stack& errors)
{
    std::string message = "glTF parse failed";
    for (uint32_t i = 0; i < tg3_errors_count(&errors); ++i) {
        const tg3_error_entry* entry = tg3_errors_get(&errors, i);
        if (entry && entry->severity == TG3_SEVERITY_ERROR && entry->message) {
            message += ": ";
            message += entry->message;
            if (entry->json_path) {
                message += " at ";
                message += entry->json_path;
            }
            break;
        }
    }
    return message;
}

std::unique_ptr<tg3_model, ModelDeleter> ParseModel(const std::filesystem::path& inputPath,
    ContentFingerprint& fingerprint)
{
    std::error_code error;
    const auto path = std::filesystem::absolute(inputPath, error).lexically_normal();
    if (error) throw std::runtime_error("cannot resolve model path: " + inputPath.string());
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open glTF model: " + path.string());
    const auto end = file.tellg();
    if (end <= 0 || static_cast<uint64_t>(end) > kMaxModelFileSize)
        throw std::runtime_error("glTF model is empty or exceeds the 128 MiB import limit");
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("failed reading glTF model: " + path.string());

    auto model = std::unique_ptr<tg3_model, ModelDeleter>(new tg3_model{});
    ErrorStack errors;
    fingerprint.AddBlock(1, bytes);
    FileContext context{ std::filesystem::weakly_canonical(path.parent_path()), &fingerprint };
    tg3_parse_options options{};
    tg3_parse_options_init(&options);
    options.validate_indices = 1;
    options.max_external_file_size = kMaxModelFileSize;
    options.memory.memory_budget = 512ull * 1024ull * 1024ull;
    options.memory.max_single_alloc = 256ull * 1024ull * 1024ull;
    options.fs.read_file = ReadExternalFile;
    options.fs.free_file = FreeExternalFile;
    options.fs.user_data = &context;

    const std::string base = PathUtf8(context.root);
    options.images_as_is = 0;
    const tg3_error_code result = tg3_parse_auto(model.get(), &errors.value,
        bytes.data(), bytes.size(), base.c_str(), static_cast<uint32_t>(base.size()), &options);
    if (result != TG3_OK || tg3_errors_has_error(&errors.value))
        throw std::runtime_error(ParserErrors(errors.value) + " ('" + path.string() + "')");
    if (model->asset.version.len == 0 ||
        !std::string(model->asset.version.data, model->asset.version.len).starts_with("2."))
        throw std::runtime_error("only glTF 2.x assets are supported");
    if (model->extensions_required_count != 0)
        throw std::runtime_error("glTF asset requires extensions not supported by this importer");
    return model;
}

const tg3_str_int_pair* FindAttribute(const tg3_primitive& primitive, const char* name)
{
    for (uint32_t i = 0; i < primitive.attributes_count; ++i) {
        if (tg3_str_equals_cstr(primitive.attributes[i].key, name)) return &primitive.attributes[i];
    }
    return nullptr;
}

const uint8_t* AccessorElement(const tg3_model& model, const tg3_accessor& accessor,
    uint64_t element, uint32_t componentBytes, uint32_t components)
{
    if (accessor.buffer_view < 0 || static_cast<uint32_t>(accessor.buffer_view) >= model.buffer_views_count ||
        element >= accessor.count || accessor.sparse.is_sparse)
        throw std::runtime_error("unsupported or invalid glTF accessor");
    const tg3_buffer_view& view = model.buffer_views[accessor.buffer_view];
    if (view.buffer < 0 || static_cast<uint32_t>(view.buffer) >= model.buffers_count)
        throw std::runtime_error("glTF accessor references an invalid buffer");
    const tg3_buffer& buffer = model.buffers[view.buffer];
    const uint64_t packedSize = static_cast<uint64_t>(componentBytes) * components;
    const int32_t strideResult = tg3_accessor_byte_stride(&accessor, &view);
    const uint64_t stride = strideResult > 0 ? static_cast<uint64_t>(strideResult) : packedSize;
    if (stride < packedSize || accessor.byte_offset > view.byte_length ||
        view.byte_offset > buffer.data.count || element > (UINT64_MAX - accessor.byte_offset) / stride)
        throw std::runtime_error("glTF accessor range overflows");
    const uint64_t relative = accessor.byte_offset + element * stride;
    if (relative > view.byte_length || packedSize > view.byte_length - relative ||
        view.byte_offset + relative > buffer.data.count ||
        packedSize > buffer.data.count - (view.byte_offset + relative))
        throw std::runtime_error("glTF accessor exceeds its buffer view");
    return buffer.data.data + view.byte_offset + relative;
}

float ReadComponent(const uint8_t* data, int32_t type, bool normalized)
{
    switch (type) {
    case TG3_COMPONENT_TYPE_FLOAT: {
        float value; std::memcpy(&value, data, sizeof(value)); return value;
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: return normalized ? *data / 255.0f : static_cast<float>(*data);
    case TG3_COMPONENT_TYPE_BYTE: {
        int8_t value; std::memcpy(&value, data, 1);
        return normalized ? std::max(-1.0f, value / 127.0f) : static_cast<float>(value);
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: {
        uint16_t value; std::memcpy(&value, data, 2);
        return normalized ? value / 65535.0f : static_cast<float>(value);
    }
    case TG3_COMPONENT_TYPE_SHORT: {
        int16_t value; std::memcpy(&value, data, 2);
        return normalized ? std::max(-1.0f, value / 32767.0f) : static_cast<float>(value);
    }
    default: throw std::runtime_error("unsupported glTF vertex component type");
    }
}

uint32_t ComponentSize(int32_t type)
{
    const int32_t size = tg3_component_size(type);
    if (size <= 0) throw std::runtime_error("invalid glTF accessor component type");
    return static_cast<uint32_t>(size);
}

glm::vec4 ReadVec4(const tg3_accessor& accessor, const tg3_model& model, uint64_t index,
    uint32_t components, const glm::vec4& fallback)
{
    if (tg3_num_components(accessor.type) != static_cast<int32_t>(components))
        throw std::runtime_error("unexpected glTF vertex attribute shape");
    const uint32_t componentSize = ComponentSize(accessor.component_type);
    const uint8_t* data = AccessorElement(model, accessor, index, componentSize, components);
    glm::vec4 result = fallback;
    for (uint32_t c = 0; c < components; ++c)
        result[c] = ReadComponent(data + c * componentSize, accessor.component_type, accessor.normalized != 0);
    return result;
}

uint32_t AccessorIndex(const tg3_model& model, const tg3_accessor& accessor, uint64_t index)
{
    if (accessor.type != TG3_TYPE_SCALAR || accessor.normalized || accessor.sparse.is_sparse)
        throw std::runtime_error("glTF primitive index accessor must be a non-normalized scalar");
    const uint8_t* data = AccessorElement(model, accessor, index,
        ComponentSize(accessor.component_type), 1);
    switch (accessor.component_type) {
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: return *data;
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: { uint16_t v; std::memcpy(&v, data, 2); return v; }
    case TG3_COMPONENT_TYPE_UNSIGNED_INT: { uint32_t v; std::memcpy(&v, data, 4); return v; }
    default: throw std::runtime_error("glTF indices must use an unsigned integer type");
    }
}

MeshData ReadPrimitiveMesh(const tg3_model& model, const tg3_primitive& primitive)
{
    if (primitive.mode >= 0 && primitive.mode != 4)
        throw std::runtime_error("only TRIANGLES glTF primitives are supported");
    const auto* positionAttribute = FindAttribute(primitive, "POSITION");
    if (!positionAttribute || positionAttribute->value < 0 ||
        static_cast<uint32_t>(positionAttribute->value) >= model.accessors_count)
        throw std::runtime_error("glTF primitive is missing POSITION");
    const tg3_accessor& positions = model.accessors[positionAttribute->value];
    if (positions.type != TG3_TYPE_VEC3 || positions.component_type != TG3_COMPONENT_TYPE_FLOAT ||
        positions.count == 0 || positions.count > 65536)
        throw std::runtime_error("POSITION must be a non-empty FLOAT VEC3 with at most 65536 vertices");

    const tg3_accessor* uv = nullptr;
    if (const auto* attribute = FindAttribute(primitive, "TEXCOORD_0")) {
        if (attribute->value < 0 || static_cast<uint32_t>(attribute->value) >= model.accessors_count)
            throw std::runtime_error("glTF TEXCOORD_0 accessor index is invalid");
        uv = &model.accessors[attribute->value];
        if (uv->type != TG3_TYPE_VEC2 || uv->count != positions.count)
            throw std::runtime_error("TEXCOORD_0 must be a VEC2 matching POSITION count");
    }
    const tg3_accessor* colors = nullptr;
    if (const auto* attribute = FindAttribute(primitive, "COLOR_0")) {
        if (attribute->value < 0 || static_cast<uint32_t>(attribute->value) >= model.accessors_count)
            throw std::runtime_error("glTF COLOR_0 accessor index is invalid");
        colors = &model.accessors[attribute->value];
        const int componentCount = tg3_num_components(colors->type);
        if ((componentCount != 3 && componentCount != 4) || colors->count != positions.count)
            throw std::runtime_error("COLOR_0 must be VEC3 or VEC4 matching POSITION count");
    }
    const tg3_accessor* normals = nullptr;
    if (const auto* attribute = FindAttribute(primitive, "NORMAL")) {
        if (attribute->value < 0 || static_cast<uint32_t>(attribute->value) >= model.accessors_count)
            throw std::runtime_error("glTF NORMAL accessor index is invalid");
        normals = &model.accessors[attribute->value];
        if (normals->type != TG3_TYPE_VEC3 || normals->component_type != TG3_COMPONENT_TYPE_FLOAT ||
            normals->count != positions.count)
            throw std::runtime_error("NORMAL must be a FLOAT VEC3 matching POSITION count");
    }

    MeshData mesh;
    mesh.vertices.reserve(static_cast<size_t>(positions.count));
    for (uint64_t i = 0; i < positions.count; ++i) {
        const glm::vec4 position = ReadVec4(positions, model, i, 3, glm::vec4(0, 0, 0, 1));
        Vertex vertex{};
        vertex.position = glm::vec3(position);
        vertex.color = glm::vec3(1.0f);
        vertex.texCoord = glm::vec2(0.0f);
        if (normals) {
            const glm::vec4 value = ReadVec4(*normals, model, i, 3, glm::vec4(0.0f));
            const glm::vec3 n(value);
            if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z) ||
                glm::dot(n, n) <= 1e-12f)
                throw std::runtime_error("glTF NORMAL contains a non-finite or zero value");
            vertex.normal = glm::normalize(n);
        }
        if (uv) {
            const glm::vec4 tex = ReadVec4(*uv, model, i, 2, glm::vec4(0.0f));
            vertex.texCoord = glm::vec2(tex);
        }
        if (colors) {
            const uint32_t colorComponents = static_cast<uint32_t>(tg3_num_components(colors->type));
            const glm::vec4 color = ReadVec4(*colors, model, i, colorComponents, glm::vec4(1.0f));
            vertex.color = glm::vec3(color);
        }
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
            !std::isfinite(vertex.position.z) || !std::isfinite(vertex.texCoord.x) ||
            !std::isfinite(vertex.texCoord.y))
            throw std::runtime_error("glTF vertex contains a non-finite value");
        mesh.vertices.push_back(vertex);
    }

    if (primitive.indices >= 0) {
        if (static_cast<uint32_t>(primitive.indices) >= model.accessors_count)
            throw std::runtime_error("glTF index accessor is invalid");
        const tg3_accessor& indices = model.accessors[primitive.indices];
        if (indices.count == 0 || indices.count % 3 != 0)
            throw std::runtime_error("glTF triangle index count must be a non-zero multiple of 3");
        mesh.indices.reserve(static_cast<size_t>(indices.count));
        for (uint64_t i = 0; i < indices.count; ++i) {
            const uint32_t value = AccessorIndex(model, indices, i);
            if (value >= positions.count || value > UINT16_MAX)
                throw std::runtime_error("glTF index references a vertex outside the supported range");
            mesh.indices.push_back(static_cast<uint16_t>(value));
        }
    } else {
        if (positions.count % 3 != 0)
            throw std::runtime_error("non-indexed glTF triangle vertex count must be a multiple of 3");
        mesh.indices.resize(static_cast<size_t>(positions.count));
        std::iota(mesh.indices.begin(), mesh.indices.end(), uint16_t{0});
    }
    if (!normals) {
        std::vector<glm::vec3> sums(mesh.vertices.size(), glm::vec3(0.0f));
        std::vector<bool> referenced(mesh.vertices.size(), false);
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const uint16_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
            referenced[ia] = referenced[ib] = referenced[ic] = true;
            const glm::vec3 n = glm::cross(mesh.vertices[ib].position - mesh.vertices[ia].position,
                mesh.vertices[ic].position - mesh.vertices[ia].position);
            if (glm::dot(n, n) <= 1e-12f) continue;
            sums[ia] += n; sums[ib] += n; sums[ic] += n;
        }
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            if (glm::dot(sums[i], sums[i]) <= 1e-12f && referenced[i])
                throw std::runtime_error("glTF primitive contains a vertex without a valid normal");
            if (referenced[i]) mesh.vertices[i].normal = glm::normalize(sums[i]);
        }
    }
    return mesh;
}

glm::mat4 NodeLocalTransform(const tg3_node& node)
{
    if (node.has_matrix) {
        glm::mat4 matrix(1.0f);
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                matrix[column][row] = static_cast<float>(node.matrix[column * 4 + row]);
        return matrix;
    }
    const glm::vec3 translation(static_cast<float>(node.translation[0]),
        static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2]));
    const glm::quat rotation(static_cast<float>(node.rotation[3]),
        static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]),
        static_cast<float>(node.rotation[2]));
    const glm::vec3 scale(static_cast<float>(node.scale[0]),
        static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2]));
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
        glm::scale(glm::mat4(1.0f), scale);
}

using PrimitiveRef = ModelPrimitiveTemplate;
using NodeRef = ModelNodeTemplate;

struct PreparedModel
{
    std::vector<MeshData> meshes;
    std::vector<SurfaceMaterial> materials;
    std::vector<Image> textures;
    std::vector<NodeRef> nodes;
    std::vector<int32_t> roots;
    // 准备阶段记录源编号，登记资产时再填入 assetId。
    std::vector<ModelMeshSource> meshSources;
    std::vector<int32_t> materialKeys;
    std::string fingerprint;
};

PreparedModel Prepare(const tg3_model& model, const std::filesystem::path& imageRoot,
    ContentFingerprint& fingerprint)
{
    PreparedModel prepared;
    if (model.materials_count == 0) {
        SurfaceMaterial defaultGltfMaterial;
        defaultGltfMaterial.metallicFactor = 1.0f;
        defaultGltfMaterial.roughnessFactor = 1.0f;
        prepared.materials.push_back(defaultGltfMaterial);
        prepared.materialKeys.push_back(-1);
    }
    std::vector<uint32_t> imageTextureMap(model.images_count, kNoTexture);
    for (uint32_t materialIndex = 0; materialIndex < model.materials_count; ++materialIndex) {
        const tg3_material& source = model.materials[materialIndex];
        SurfaceMaterial material;
        for (int i = 0; i < 4; ++i)
            material.baseColorFactor[i] = static_cast<float>(source.pbr_metallic_roughness.base_color_factor[i]);
        material.metallicFactor = static_cast<float>(source.pbr_metallic_roughness.metallic_factor);
        material.roughnessFactor = static_cast<float>(source.pbr_metallic_roughness.roughness_factor);
        for (int i = 0; i < 3; ++i)
            material.emissive[i] = static_cast<float>(source.emissive_factor[i]);
        const auto& textureInfo = source.pbr_metallic_roughness.base_color_texture;
        if (textureInfo.index >= 0) {
            if (static_cast<uint32_t>(textureInfo.index) >= model.textures_count)
                throw std::runtime_error("glTF material texture index is invalid");
            if (textureInfo.tex_coord != 0)
                throw std::runtime_error("only TEXCOORD_0 material textures are supported");
            const tg3_texture& texture = model.textures[textureInfo.index];
            if (texture.source < 0 || static_cast<uint32_t>(texture.source) >= model.images_count)
                throw std::runtime_error("glTF texture image source is invalid");
            const uint32_t imageIndex = static_cast<uint32_t>(texture.source);
            if (imageTextureMap[imageIndex] == kNoTexture) {
                const tg3_image& image = model.images[imageIndex];
                Image decoded = DecodeModelImage(model, image, imageRoot, fingerprint);
                if (decoded.width > 16384 || decoded.height > 16384 ||
                    static_cast<uint64_t>(decoded.width) * decoded.height > (128ull * 1024ull * 1024ull) / 4)
                    throw std::runtime_error("glTF texture exceeds the supported image size");
                imageTextureMap[imageIndex] = static_cast<uint32_t>(prepared.textures.size());
                prepared.textures.push_back(std::move(decoded));
            }
            material.baseColorTexture = imageTextureMap[imageIndex];
        }
        prepared.materials.push_back(material);
        prepared.materialKeys.push_back(static_cast<int32_t>(materialIndex));
    }

    uint32_t defaultMaterial = 0;
    bool needsDefault = false;
    for (uint32_t m = 0; m < model.meshes_count; ++m)
        for (uint32_t p = 0; p < model.meshes[m].primitives_count; ++p)
            needsDefault |= model.meshes[m].primitives[p].material < 0;
    // 混合有/无材质的模型需要独立 glTF 缺省材质，不能回退到显式材质 0。
    if (model.materials_count > 0 && needsDefault) {
        defaultMaterial = static_cast<uint32_t>(prepared.materials.size());
        SurfaceMaterial fallback;
        fallback.metallicFactor = fallback.roughnessFactor = 1.0f;
        prepared.materials.push_back(fallback);
        prepared.materialKeys.push_back(-1);
    }
    std::vector<uint32_t> meshOffsets(model.meshes_count, 0);
    for (uint32_t meshIndex = 0; meshIndex < model.meshes_count; ++meshIndex) {
        const tg3_mesh& mesh = model.meshes[meshIndex];
        meshOffsets[meshIndex] = static_cast<uint32_t>(prepared.meshes.size());
        for (uint32_t p = 0; p < mesh.primitives_count; ++p) {
            prepared.meshes.push_back(ReadPrimitiveMesh(model, mesh.primitives[p]));
            prepared.meshSources.push_back(ModelMeshSource{ {}, meshIndex, p });
        }
    }

    std::vector<uint8_t> hasParent(model.nodes_count, 0);
    for (uint32_t i = 0; i < model.nodes_count; ++i) {
        const tg3_node& node = model.nodes[i];
        for (uint32_t child = 0; child < node.children_count; ++child) {
            const int32_t index = node.children[child];
            if (index < 0 || static_cast<uint32_t>(index) >= model.nodes_count)
                throw std::runtime_error("glTF node child index is invalid");
            if (hasParent[index]++) throw std::runtime_error("glTF node has more than one parent");
        }
    }
    if (model.default_scene >= 0) {
        if (static_cast<uint32_t>(model.default_scene) >= model.scenes_count)
            throw std::runtime_error("glTF default scene index is invalid");
        const tg3_scene& scene = model.scenes[model.default_scene];
        for (uint32_t i = 0; i < scene.nodes_count; ++i) prepared.roots.push_back(scene.nodes[i]);
    } else {
        for (uint32_t i = 0; i < model.nodes_count; ++i)
            if (!hasParent[i]) prepared.roots.push_back(static_cast<int32_t>(i));
    }

    std::vector<uint8_t> visiting(model.nodes_count, 0);
    std::function<void(int32_t, int32_t)> visit = [&](int32_t index, int32_t parentIndex) {
        if (index < 0 || static_cast<uint32_t>(index) >= model.nodes_count)
            throw std::runtime_error("glTF scene root index is invalid");
        if (visiting[index] == 1) throw std::runtime_error("glTF node hierarchy contains a cycle");
        if (visiting[index] == 2) return;
        visiting[index] = 1;
        const tg3_node& node = model.nodes[index];
        if (node.mesh >= 0) {
            if (static_cast<uint32_t>(node.mesh) >= model.meshes_count)
                throw std::runtime_error("glTF node mesh index is invalid");
            const tg3_mesh& mesh = model.meshes[node.mesh];
            NodeRef ref;
            ref.sourceIndex = static_cast<uint32_t>(index);
            ref.parentIndex = parentIndex;
            ref.name = node.name.data && node.name.len
                ? std::string(node.name.data, node.name.len)
                : "Node " + std::to_string(index);
            ref.localTransform = NodeLocalTransform(node);
            for (uint32_t p = 0; p < mesh.primitives_count; ++p) {
                const int32_t material = mesh.primitives[p].material;
                if (material >= 0 && static_cast<uint32_t>(material) >= model.materials_count)
                    throw std::runtime_error("glTF primitive material index is invalid");
                ref.primitives.push_back(PrimitiveRef{
                    meshOffsets[node.mesh] + p,
                    material >= 0 ? static_cast<uint32_t>(material) : defaultMaterial,
                    p });
            }
            prepared.nodes.push_back(std::move(ref));
        } else {
            NodeRef ref;
            ref.sourceIndex = static_cast<uint32_t>(index);
            ref.parentIndex = parentIndex;
            ref.name = node.name.data && node.name.len
                ? std::string(node.name.data, node.name.len)
                : "Node " + std::to_string(index);
            ref.localTransform = NodeLocalTransform(node);
            prepared.nodes.push_back(std::move(ref));
        }
        for (uint32_t child = 0; child < node.children_count; ++child) visit(node.children[child], index);
        visiting[index] = 2;
    };
    for (int32_t root : prepared.roots) visit(root, -1);
    if (prepared.meshes.empty()) throw std::runtime_error("glTF scene contains no triangle meshes");
    return prepared;
}

void AppendResources(Scene& scene, PreparedModel& prepared,
    std::vector<uint32_t>& meshMap, std::vector<uint32_t>& materialMap)
{
    const uint32_t firstTexture = scene.materials.TextureCount();
    for (Image& texture : prepared.textures) scene.materials.AddTexture(std::move(texture));
    materialMap.reserve(prepared.materials.size());
    for (SurfaceMaterial material : prepared.materials) {
        if (material.baseColorTexture != kNoTexture) material.baseColorTexture += firstTexture;
        materialMap.push_back(scene.materials.AddMaterial(material));
    }
    if (materialMap.empty()) materialMap.push_back(0);
    meshMap.reserve(prepared.meshes.size());
    for (MeshData& mesh : prepared.meshes) meshMap.push_back(scene.meshes.Append(std::move(mesh)));
}

PreparedModel LoadPrepared(const std::filesystem::path& path)
{
    ContentFingerprint fingerprint;
    auto parsed = ParseModel(path, fingerprint);
    auto prepared = Prepare(*parsed, std::filesystem::absolute(path).parent_path(), fingerprint);
    prepared.fingerprint = "sha256-import-v1:" + fingerprint.Hex();
    return prepared;
}

std::string AssetPathKey(const std::filesystem::path& path)
{
    // weakly_canonical 也能在缓存命中时处理已经移走的源文件。
    return PathUtf8(std::filesystem::weakly_canonical(std::filesystem::absolute(path)));
}

uint32_t FindAsset(const Scene& scene, const std::string& key)
{
    const auto source = PathFromUtf8(key.data(), key.size());
    for (uint32_t i = 0; i < scene.modelAssets.size(); ++i) {
        const std::string& stored = scene.modelAssets[i].sourcePath;
        if (stored == key) return i;
        // 对现存文件识别大小写/链接别名；文件不存在时仍可精确命中缓存。
        std::error_code error;
        if (std::filesystem::equivalent(source, PathFromUtf8(stored.data(), stored.size()), error) && !error)
            return i;
    }
    return UINT32_MAX;
}

// 缓存未命中时，读取并登记一份新模型资产；只加载资源和节点模板，不创建实例 Entity。
// path 是本次读取的文件路径，key 是规范化后的路径缓存键，不是资产 ID。
// savedId / expectedFingerprint 在 Load 场景时用于恢复资产身份并校验源版本；普通 Import 可为空。
// 返回值是 Scene.modelAssets 的运行时数组下标，供 InstantiateAsset 使用，不是持久 ID。
// 本函数会追加 Scene 资源；编辑器在候选 Scene 中调用，成功后才替换当前编辑场景。
uint32_t AppendAsset(Scene& scene, const std::filesystem::path& path, const std::string& key,
    const std::string& savedId, const std::string& expectedFingerprint)
{
    // 1. 在临时 prepared 中准备网格、材质、图片和节点模板，并计算实际读取内容的指纹。
    // 此时模板的 mesh/material 是资产局部编号，来源中的 sourceMesh/primitive 是 glTF 编号。
    PreparedModel prepared = LoadPrepared(path);
    // Load 要求源内容仍与保存时一致；不一致就在追加资源之前拒绝。
    if (!expectedFingerprint.empty() && prepared.fingerprint != expectedFingerprint)
        throw std::runtime_error("model content changed; reload rejected: " + key);
    ModelAsset asset;
    asset.sourcePath = key;

    // 2. 建立资产身份。Import 分配新 ID（如 model-1）；Load 沿用场景文件里的 savedId。
    // LoadAsset 已处理缓存命中，同一资产复用时不会进入这里再次分配 ID。
    asset.id = savedId.empty() ? scene.AllocateModelAssetId() : savedId;

    // ID 在当前 Scene 内必须唯一；指纹保存本次导入基线，之后 Save 不重新计算它。
    if (std::any_of(scene.modelAssets.begin(), scene.modelAssets.end(),
        [&](const ModelAsset& item) { return item.id == asset.id; }))
        throw std::runtime_error("duplicate model asset ID: " + asset.id);
    asset.fingerprint = prepared.fingerprint;

    // 3. 必须在追加图片之前记录起点。例如已有 3 张、新增 2 张，新图片的 Scene 索引是 3、4。
    // 若追加后才读取 TextureCount，起点会变成 5，得到的图片索引将越界。
    const uint32_t firstTexture = scene.materials.TextureCount();
    const uint32_t textureCount = static_cast<uint32_t>(prepared.textures.size());

    // 追加资源到 Scene 库，建立“资产局部编号 -> Scene 运行时索引”的映射。
    // 例如 asset.meshIndices[1] = 7：模板 mesh=1 的部分在 Scene 网格库中是第 7 份。
    // 材质中的图片局部编号也在 AppendResources 内转换为 Scene 图片索引。
    AppendResources(scene, prepared, asset.meshIndices, asset.materialIndices);

    // 4. 为每份新网格登记来源，供 Save/Load 在运行时索引与持久引用之间转换。
    // i 是资产局部编号；map 键 asset.meshIndices[i] 是 Scene 网格索引。
    // source 中的 sourceMesh/primitive 已在准备阶段记录，这里补上所属资产的 ID。
    for (size_t i = 0; i < asset.meshIndices.size(); ++i) {
        ModelMeshSource source = prepared.meshSources.at(i);
        source.assetId = asset.id;
        scene.modelMeshSources.emplace(asset.meshIndices[i], std::move(source));
    }

    // 材质来源同理：map 键是 Scene 材质索引，值记录资产 ID 和 glTF 材质编号（-1 为 glTF 默认材质）。
    for (size_t i = 0; i < asset.materialIndices.size(); ++i)
        scene.modelMaterialSources.emplace(asset.materialIndices[i],
            ModelMaterialSource{ asset.id, prepared.materialKeys.at(i) });

    // 5. 图片按准备顺序连续追加，用“追加前起点 + 图片局部编号”记录 Scene 图片索引。
    for (uint32_t i = 0; i < textureCount; ++i) asset.textureIndices.push_back(firstTexture + i);
    // 接管节点模板；每次实例化都复用这些模板，再创建独立的 Entity 和局部变换。
    asset.nodes = std::move(prepared.nodes);

    // 6. 登记资产。追加前的数组长度就是这份资产的 assetIndex，与字符串 asset.id 是两回事。
    if (scene.modelAssets.size() >= UINT32_MAX)
        throw std::runtime_error("Scene is out of model asset indices");
    const uint32_t index = static_cast<uint32_t>(scene.modelAssets.size());
    scene.modelAssets.push_back(std::move(asset));
    // prepared 等临时对象在返回时销毁；资源、来源记录和模板已由 Scene 持有。
    return index;
}

} // namespace

uint32_t ModelImporter::LoadAsset(Scene& scene, const std::filesystem::path& path,
    const std::string& savedId, const std::string& expectedFingerprint)
{
    if (savedId.size() > 128 || savedId.find('\0') != std::string::npos)
        throw std::runtime_error("invalid model asset ID");
    const std::string key = AssetPathKey(path);
    const uint32_t existing = FindAsset(scene, key);
    if (existing != UINT32_MAX) {
        const auto& asset = scene.modelAssets[existing];
        if ((!savedId.empty() && savedId != asset.id) ||
            (!expectedFingerprint.empty() && expectedFingerprint != asset.fingerprint))
            throw std::runtime_error("conflicting model asset reference: " + key);
        return existing;
    }
    return AppendAsset(scene, path, key, savedId, expectedFingerprint);
}

// 单独导入 model 时使用
std::vector<Entity> ModelImporter::Import(Scene& scene, const std::filesystem::path& path)
{
    return InstantiateAsset(scene, LoadAsset(scene, path));
}

std::vector<Entity> ModelImporter::InstantiateAsset(Scene& scene, uint32_t assetIndex)
{
    if (assetIndex >= scene.modelAssets.size())
        throw std::runtime_error("model asset index is out of range");
    const ModelAsset& asset = scene.modelAssets[assetIndex];

    // 引擎实例根保存整体变换，源 glTF 的每个根仍保留自己的 localTransform。
    const Entity instanceRoot = scene.registry.Create();
    scene.registry.Add<Transform>(instanceRoot);
    const auto sourcePath = PathFromUtf8(asset.sourcePath.data(), asset.sourcePath.size());
    scene.registry.Add<Name>(instanceRoot, Name{ PathUtf8(sourcePath.stem()) + " Instance" });

    std::vector<Entity> entities;
    uint32_t maximumNode = 0;
    for (const NodeRef& node : asset.nodes) maximumNode = std::max(maximumNode, node.sourceIndex);
    entities.resize(asset.nodes.empty() ? 0 : static_cast<size_t>(maximumNode) + 1);
    std::vector<uint8_t> created(entities.size(), 0);
    std::vector<Entity> roots{ instanceRoot };

    // 先序准备好的节点，所以每个父节点的 Entity 已经创建好了。
    for (const NodeRef& ref : asset.nodes) {
        const Entity entity = scene.registry.Create();
        entities[ref.sourceIndex] = entity;
        created[ref.sourceIndex] = 1;

        Transform transform;
        transform.local = ref.localTransform;
        transform.world = transform.local;
        scene.registry.Add<Transform>(entity, transform);
        scene.registry.Add<Name>(entity, Name{ ref.name });
        if (ref.parentIndex >= 0) {
            if (static_cast<size_t>(ref.parentIndex) >= created.size() || !created[ref.parentIndex])
                throw std::runtime_error("glTF hierarchy was not emitted in parent-first order");
            scene.registry.Add<Parent>(entity, Parent{ entities[ref.parentIndex] });
        } else {
            scene.registry.Add<Parent>(entity, Parent{ instanceRoot });
        }

        for (const PrimitiveRef& primitive : ref.primitives) {
            const Entity part = scene.registry.Create();
            Transform partTransform;
            partTransform.world = partTransform.local;
            scene.registry.Add<Transform>(part, partTransform);
            scene.registry.Add<Name>(part, Name{ ref.name + " / Primitive " + std::to_string(primitive.ordinal) });
            scene.registry.Add<Parent>(part, Parent{ entity });
            scene.registry.Add<MeshRenderer>(part,
                MeshRenderer{ asset.meshIndices.at(primitive.mesh), asset.materialIndices.at(primitive.material) });
        }
    }
    return roots;
}
