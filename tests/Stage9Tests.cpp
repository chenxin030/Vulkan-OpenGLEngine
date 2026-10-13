#include "scene/TangentSpace.h"
#include "scene/Scene.h"
#include "scene/ModelImporter.h"
#include "scene/Components.h"
#include "scene/RenderSnapshot.h"
#include "scene/SceneUpdater.h"
#include "scene/SceneSerializer.h"
#include "editor/EditorGeometry.h"
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <limits>

namespace {
using Json=nlohmann::json;
template<class F> bool Throws(F&& f) { try { f(); } catch(const std::exception&) { return true; } return false; }
MeshData Quad() {
    MeshData m;
    for (const auto p: {glm::vec2(0,0),glm::vec2(1,0),glm::vec2(1,1),glm::vec2(0,1)}) {
        Vertex v{}; v.position=glm::vec3(p,0); v.color=glm::vec3(1); v.texCoord=p;
        m.vertices.push_back(v);
    }
    m.indices={0,1,2,0,2,3}; return m;
}
// 生成小型真实 glTF，而非伪造 PreparedModel；因此可检查解析→资源→快照→v3 链路。
Json Fixture(const std::filesystem::path& dir) {
    Json j={{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",Json::array({{{"nodes",{0}}}})},
        {"nodes",Json::array({{{"mesh",0},{"name","Normal fixture"}}})}};
    j["bufferViews"]=Json::array(); j["accessors"]=Json::array();
    std::vector<uint8_t> buffer;
    auto attribute=[&](std::vector<float> values,int components) {
        const size_t offset=buffer.size(); buffer.resize(offset+values.size()*sizeof(float));
        std::memcpy(buffer.data()+offset,values.data(),values.size()*sizeof(float));
        const uint32_t a=static_cast<uint32_t>(j["accessors"].size());
        j["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",values.size()*sizeof(float)}});
        j["accessors"].push_back({{"bufferView",a},{"componentType",5126},{"count",values.size()/components},
            {"type",components==2?"VEC2":components==3?"VEC3":"VEC4"}});
        return a;
    };
    auto positions=attribute({0,0,0,1,0,0,0,1,0},3);
    j["accessors"][positions]["min"]={0,0,0}; j["accessors"][positions]["max"]={1,1,0};
    auto normals=attribute({0,0,1,0,0,1,0,0,1},3);
    auto uv=attribute({0,0,1,0,0,1},2);
    auto tangent=attribute({1,0,0,1,1,0,0,1,1,0,0,1},4);
    j["meshes"]=Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",positions},{"NORMAL",normals},{"TEXCOORD_0",uv},{"TANGENT",tangent}}},{"material",0}}})}}});
    j["materials"]=Json::array({{{"normalTexture",{{"index",0},{"scale",0.7}}},
        {"pbrMetallicRoughness",{{"baseColorTexture",{{"index",0}}},{"metallicFactor",0},{"roughnessFactor",0.4}}}}});
    j["textures"]=Json::array({{{"source",0}}});
    j["images"]=Json::array({{{"uri","data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z/D/PwAG/gL+DHWJ3gAAAABJRU5ErkJggg=="}}});
    j["buffers"]=Json::array({{{"byteLength",buffer.size()},{"uri","stage9.bin"}}});
    std::ofstream file(dir/"stage9.bin",std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(buffer.data()),buffer.size());
    return j;
}
void Write(const std::filesystem::path& path,const Json& j) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc); file<<j.dump();
}
} // namespace

bool Stage9Checks(int& assertions) {
#define VERIFY(...) do { ++assertions; if (!(__VA_ARGS__)) { std::cerr<<"Stage9 check failed at "<<__LINE__<<": " #__VA_ARGS__ <<'\n'; return false; } } while(false)
    auto q=Quad(); GenerateTangents(q);
    VERIFY(q.vertices.size()==4 && q.indices.size()==6);
    for (const auto& v:q.vertices) {
        VERIFY(glm::length(glm::vec3(v.tangent)-glm::vec3(1,0,0))<1e-5f);
        VERIFY(v.tangent.w==1 && std::abs(glm::dot(glm::vec3(v.tangent),v.normal))<1e-5f);
    }
    auto mirror=Quad(); for(auto& v:mirror.vertices) v.texCoord.x=1-v.texCoord.x; GenerateTangents(mirror);
    VERIFY(mirror.vertices[0].tangent.x < -0.99f && mirror.vertices[0].tangent.w==-1);
    auto rotatedUv=Quad(); for(auto& v:rotatedUv.vertices) v.texCoord={v.texCoord.y,1-v.texCoord.x}; GenerateTangents(rotatedUv);
    VERIFY(glm::length(glm::vec3(rotatedUv.vertices[0].tangent)-glm::vec3(0,1,0))<1e-5f);
    VERIFY(rotatedUv.vertices[0].tangent.w==1);
    // 两个相邻三角形的 UV 面积符号相反，共享角点必须拆开。
    auto seam=Quad(); seam.vertices[3].texCoord={1,0}; GenerateTangents(seam);
    VERIFY(seam.vertices.size()==6);
    VERIFY(seam.vertices[seam.indices[0]].tangent.w!=seam.vertices[seam.indices[3]].tangent.w);
    auto hard=Quad(); hard.vertices[3].position={0,0,1}; GenerateFlatNormals(hard);
    VERIFY(hard.vertices.size()==6);
    VERIFY(glm::length(hard.vertices[hard.indices[0]].normal-glm::vec3(0,0,1))<1e-6f);
    VERIFY(glm::dot(hard.vertices[hard.indices[3]].normal,glm::vec3(0,0,1))<0.01f);
    auto coplanar=Quad(); GenerateFlatNormals(coplanar); VERIFY(coplanar.vertices.size()==4);
    auto tiny=Quad(); for(auto& v:tiny.vertices) v.position*=0.001f; GenerateTangents(tiny);
    VERIFY(glm::length(glm::vec3(tiny.vertices[0].tangent)-glm::vec3(1,0,0))<1e-5f);
    tiny=Quad(); for(auto& v:tiny.vertices) v.position*=1e-12f; GenerateFlatNormals(tiny);
    VERIFY(tiny.vertices[0].normal.z>0.99f);
    auto invalid=Quad(); invalid.vertices[0].tangent.w=0; VERIFY(Throws([&]{ValidateTangents(invalid);}));
    invalid=Quad(); invalid.vertices[0].tangent.w=-1; VERIFY(Throws([&]{ValidateTangents(invalid);}));
    invalid=Quad(); invalid.vertices[0].tangent={0,0,1,1}; VERIFY(Throws([&]{ValidateTangents(invalid);}));
    invalid=Quad(); invalid.vertices[0].tangent.x=std::numeric_limits<float>::infinity(); VERIFY(Throws([&]{ValidateTangents(invalid);}));
    invalid=Quad(); for(auto& v:invalid.vertices) v.texCoord={0,0}; VERIFY(Throws([&]{GenerateTangents(invalid);}));
    invalid=Quad(); invalid.vertices[1].position=invalid.vertices[0].position; VERIFY(Throws([&]{GenerateFlatNormals(invalid);}));
    invalid=Quad(); invalid.vertices[1].position=invalid.vertices[0].position; VERIFY(Throws([&]{GenerateTangents(invalid);}));
    invalid=Quad(); invalid.indices[0]=100; VERIFY(Throws([&]{GenerateTangents(invalid);}));
    invalid=Quad(); invalid.indices.pop_back(); VERIFY(Throws([&]{GenerateTangents(invalid);}));
    MeshData overflow;
    for(int i=0;i<65535;++i) {
        Vertex v{}; v.position=i%3==0?glm::vec3(0,0,0):i%3==1?glm::vec3(1,0,0):glm::vec3(0,1,0);
        overflow.vertices.push_back(v); overflow.indices.push_back(static_cast<uint16_t>(i));
    }
    overflow.vertices.push_back(Vertex{}); overflow.vertices.back().position={0,0,1};
    overflow.indices.insert(overflow.indices.end(),{0,1,65535});
    VERIFY(Throws([&]{GenerateFlatNormals(overflow);}));

    MaterialLibrary materials;
    const auto color=materials.AddTexture(Image{1,1,{128,128,255,255},true});
    const auto normal=materials.AddTexture(Image{1,1,{128,128,255,255},false});
    SurfaceMaterial mat; mat.baseColorTexture=color; mat.normalTexture=normal; mat.normalScale=-0.5f;
    VERIFY(materials.GetMaterial(materials.AddMaterial(mat)).normalScale==-0.5f);
    mat.normalTexture=color; VERIFY(Throws([&]{materials.AddMaterial(mat);}));
    mat.normalTexture=normal; mat.normalScale=std::numeric_limits<float>::infinity(); VERIFY(Throws([&]{materials.AddMaterial(mat);}));
    mat.normalScale=1; mat.normalTexture=999; VERIFY(Throws([&]{materials.AddMaterial(mat);}));

    const auto dir=std::filesystem::temp_directory_path()/"rebuild-stage9-tests";
    std::filesystem::create_directories(dir);
    const auto path=dir/"fixture.gltf";
    const auto original=Fixture(dir); Write(path,original);
    Scene scene; auto roots=ModelImporter::Import(scene,path); SceneUpdater updater; updater.Update(scene,0);
    VERIFY(roots.size()==1 && scene.modelAssets.size()==1);
    VERIFY(scene.materials.TextureCount()==2);
    auto snap=BuildRenderSnapshot(scene); VERIFY(snap.drawItems.size()==1);
    const auto& draw=snap.drawItems[0]; const auto& sourceMat=scene.materials.GetMaterial(draw.materialIndex);
    VERIFY(draw.normalTextureIndex==sourceMat.normalTexture && std::abs(draw.normalScale-0.7f)<1e-6f);
    VERIFY(!scene.materials.GetTexture(draw.normalTextureIndex).srgb);
    VERIFY(scene.materials.GetTexture(draw.textureIndex).srgb);
    const auto& imported=scene.meshes.Get(draw.meshIndex);
    VERIFY(imported.vertices[0].tangent==glm::vec4(1,0,0,1));
    const auto meshCount=scene.meshes.Count(); const auto materialCount=scene.materials.MaterialCount();
    ModelImporter::InstantiateAsset(scene,0); VERIFY(scene.meshes.Count()==meshCount && scene.materials.MaterialCount()==materialCount);
    scene.normalMapping=false; scene.normalDebug=2;
    Scene play=scene; play.normalMapping=true; play.normalDebug=0;
    VERIFY(!scene.normalMapping && scene.normalDebug==2);
    snap=BuildRenderSnapshot(scene); VERIFY(!snap.normalMapping && snap.normalDebug==2);
    scene.registry.TryGet<Transform>(roots[0])->local=glm::scale(glm::mat4(1),glm::vec3(-2,3,0.5f));
    updater.Update(scene,0); snap=BuildRenderSnapshot(scene); VERIFY(snap.drawItems[0].mirrored);
    Scene pickScene; const auto pickRoots=ModelImporter::Import(pickScene,path);
    pickScene.camera.position={0.2f,0.2f,4}; pickScene.camera.yaw=-1.57079632679f; pickScene.camera.pitch=0;
    updater.Update(pickScene,0);
    VERIFY(EditorGeometry::Pick(pickScene,{600,400},{1200,800},1.5f)==pickRoots[0]);
    pickScene.registry.TryGet<Transform>(pickRoots[0])->local=glm::scale(glm::mat4(1),glm::vec3(-1,1,1));
    pickScene.camera.position.x=-0.2f; updater.Update(pickScene,0);
    VERIFY(EditorGeometry::Pick(pickScene,{600,400},{1200,800},1.5f)==pickRoots[0]);
    pickScene.registry.TryGet<Transform>(pickRoots[0])->local=glm::rotate(glm::mat4(1),3.14159265359f,glm::vec3(0,1,0));
    updater.Update(pickScene,0);
    VERIFY(!EditorGeometry::Pick(pickScene,{600,400},{1200,800},1.5f).IsValid());
    scene.registry.TryGet<Transform>(roots[0])->local=glm::scale(glm::mat4(1),glm::vec3(0,3,0.5f));
    updater.Update(scene,0); snap=BuildRenderSnapshot(scene); VERIFY(snap.drawItems.size()==1);
    VERIFY(snap.skippedDegenerateDraws==1);
    VERIFY(IsRenderableTransform(glm::scale(glm::mat4(1),glm::vec3(1e-5f,3,0.5f))));
    VERIFY(!IsRenderableTransform(glm::scale(glm::mat4(1),glm::vec3(1e-30f))));
    auto nonfinite=glm::mat4(1);nonfinite[3][0]=std::numeric_limits<float>::infinity();
    VERIFY(!IsRenderableTransform(nonfinite));
    Scene spriteScene; const Entity sprite=spriteScene.registry.Create();
    Transform spriteTransform; spriteTransform.local=glm::scale(glm::mat4(1),glm::vec3(-1,1,1));
    spriteScene.registry.Add<Transform>(sprite,spriteTransform); spriteScene.registry.Add<SpriteRenderer>(sprite);
    updater.Update(spriteScene,0);
    VERIFY(!BuildRenderSnapshot(spriteScene).drawItems[0].mirrored);
    VERIFY(scene.registry.TryGet<Transform>(roots[0])->local[0][0]==0);
    const auto saved=dir/"roundtrip.scene.json"; SceneSerializer::Save(scene,saved);
    auto loaded=SceneSerializer::Load(saved); updater.Update(loaded,0);
    VERIFY(loaded.normalMapping && loaded.normalDebug==0);
    VERIFY(loaded.modelAssets[0].fingerprint==scene.modelAssets[0].fingerprint);
    VERIFY(loaded.materials.GetMaterial(loaded.modelAssets[0].materialIndices[0]).normalScale==sourceMat.normalScale);

    auto j=original; j["meshes"][0]["primitives"][0]["attributes"].erase("TANGENT"); Write(path,j);
    Scene generated; ModelImporter::Import(generated,path); const auto& mg=generated.meshes.Get(generated.modelAssets[0].meshIndices[0]);
    VERIFY(glm::length(glm::vec3(mg.vertices[0].tangent)-glm::vec3(1,0,0))<1e-5f);
    j["meshes"][0]["primitives"][0]["attributes"].erase("NORMAL"); Write(path,j);
    Scene flat; ModelImporter::Import(flat,path); VERIFY(flat.meshes.Get(flat.modelAssets[0].meshIndices[0]).vertices[0].normal.z>0.99f);
    j["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]=3; j["accessors"][3]["type"]="VEC3"; Write(path,j);
    Scene ignored; VERIFY(!Throws([&]{ModelImporter::Import(ignored,path);}));
    // 错误准备阶段不能追加资源或实体。
    int badIndex=0;
    auto reject=[&](Json bad) { const auto badPath=dir/("bad-"+std::to_string(badIndex++)+".gltf"); Write(badPath,bad); return Throws([&]{ModelImporter::Import(scene,badPath);}) && scene.meshes.Count()==meshCount && scene.materials.MaterialCount()==materialCount; };
    j=original; j["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0"); VERIFY(reject(j));
    j=original; j["materials"][0]["normalTexture"]["texCoord"]=1; VERIFY(reject(j));
    j=original; j["materials"][0]["normalTexture"]["index"]=99; VERIFY(reject(j));
    j=original; j["accessors"][3]["count"]=2; VERIFY(reject(j));
    j=original; j["accessors"][3]["componentType"]=5123; VERIFY(reject(j));
    j=original; j["materials"][0]["normalTexture"]["scale"]=1e100; VERIFY(reject(j));
    j=original; j["materials"][0]["normalTexture"]["scale"]=0; Write(path,j);
    Scene zero; ModelImporter::Import(zero,path); VERIFY(zero.materials.GetMaterial(zero.modelAssets[0].materialIndices[0]).normalScale==0);
    VERIFY(Throws([&]{SceneSerializer::Load(saved);})); // 源内容变化严格拒绝。
    Write(path,original);
    VERIFY(!Throws([&]{SceneSerializer::Load(saved);}));
    j=original; j["materials"][0].erase("normalTexture"); Write(path,j);
    Scene absent; ModelImporter::Import(absent,path); VERIFY(absent.materials.TextureCount()==1);
    VERIFY(absent.materials.GetMaterial(absent.modelAssets[0].materialIndices[0]).normalTexture==UINT32_MAX);
#undef VERIFY
    return true;
}
