#include "TangentSpace.h"
#include "../../third_party/MikkTSpace/mikktspace.h"
#include <array>
#include <bit>
#include <cmath>
#include <climits>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
bool Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// 相对面积判断避免把毫米级合法三角形当退化面；double 中间值避免长度平方下溢。
// 返回单位面法线。只检查几何，不改变输入；用于平面法线与生成切线的准备阶段。
glm::vec3 FaceNormal(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const glm::dvec3 e1=glm::dvec3(b)-glm::dvec3(a), e2=glm::dvec3(c)-glm::dvec3(a);
    const glm::dvec3 n=glm::cross(e1,e2);
    const double areaSquared=glm::dot(n,n), edgeProduct=glm::dot(e1,e1)*glm::dot(e2,e2);
    if (!std::isfinite(areaSquared) || !std::isfinite(edgeProduct) || edgeProduct==0 ||
        areaSquared<=edgeProduct*1e-12)
        throw std::runtime_error("cannot build tangent space for a degenerate geometry triangle");
    return glm::vec3(glm::normalize(n));
}

// 以原顶点和基底的浮点位模式为键。不会合并属性不同的顶点，也不平均镜像手性。
using Key = std::array<uint32_t, 5>;
Key BasisKey(uint32_t source, glm::vec4 basis) {
    return { source, std::bit_cast<uint32_t>(basis.x), std::bit_cast<uint32_t>(basis.y),
        std::bit_cast<uint32_t>(basis.z), std::bit_cast<uint32_t>(basis.w) };
}
uint16_t AppendCorner(MeshData& output, std::map<Key, uint16_t>& lookup,
    Key key, const Vertex& vertex) {
    if (const auto found = lookup.find(key); found != lookup.end()) return found->second;
    if (output.vertices.size() >= 65536)
        throw std::runtime_error("tangent/flat-normal vertex splitting exceeds 16-bit mesh capacity");
    const auto index = static_cast<uint16_t>(output.vertices.size());
    output.vertices.push_back(vertex);
    lookup.emplace(key, index);
    return index;
}
void CheckTopology(const MeshData& mesh) {
    if (!mesh.IsValid() || mesh.indices.size() % 3 != 0 || mesh.indices.size() / 3 > INT_MAX)
        throw std::runtime_error("tangent generation requires an indexed triangle mesh");
    for (uint16_t i : mesh.indices)
        if (i >= mesh.vertices.size()) throw std::runtime_error("tangent mesh index out of range");
}

struct MikkInput { const MeshData& mesh; std::vector<glm::vec4> corners; };
// Mikk 回调只适配现有索引网格；face/corner 表示三角形编号/角点编号。
MikkInput& Input(const SMikkTSpaceContext* c) { return *static_cast<MikkInput*>(c->m_pUserData); }
const Vertex& Corner(const SMikkTSpaceContext* c, int face, int corner) {
    const auto& m = Input(c).mesh;
    return m.vertices[m.indices[static_cast<size_t>(face) * 3 + corner]];
}
int Faces(const SMikkTSpaceContext* c) { return static_cast<int>(Input(c).mesh.indices.size() / 3); }
int Corners(const SMikkTSpaceContext*, int) { return 3; }
void Position(const SMikkTSpaceContext* c, float out[], int face, int corner) {
    const auto v = Corner(c, face, corner).position; out[0]=v.x; out[1]=v.y; out[2]=v.z;
}
void Normal(const SMikkTSpaceContext* c, float out[], int face, int corner) {
    const auto v = Corner(c, face, corner).normal; out[0]=v.x; out[1]=v.y; out[2]=v.z;
}
void Uv(const SMikkTSpaceContext* c, float out[], int face, int corner) {
    const auto v = Corner(c, face, corner).texCoord; out[0]=v.x; out[1]=v.y;
}
void SetTangent(const SMikkTSpaceContext* c, const float v[], float sign, int face, int corner) {
    Input(c).corners[static_cast<size_t>(face)*3+corner] = {v[0],v[1],v[2],sign};
}
} // namespace

void GenerateFlatNormals(MeshData& mesh) {
    CheckTopology(mesh);
    MeshData output;
    std::map<Key,uint16_t> lookup;
    output.indices.reserve(mesh.indices.size());
    for (size_t f=0; f<mesh.indices.size(); f+=3) {
        const glm::vec3 a=mesh.vertices[mesh.indices[f]].position;
        const glm::vec3 b=mesh.vertices[mesh.indices[f+1]].position;
        const glm::vec3 c=mesh.vertices[mesh.indices[f+2]].position;
        const glm::vec3 n=FaceNormal(a,b,c);
        for (size_t k=0;k<3;++k) {
            const uint16_t source=mesh.indices[f+k];
            Vertex v=mesh.vertices[source]; v.normal=n;
            output.indices.push_back(AppendCorner(output,lookup,BasisKey(source,glm::vec4(n,0)),v));
        }
    }
    mesh=std::move(output);
}

void ValidateTangents(MeshData& mesh) {
    CheckTopology(mesh);
    for (Vertex& v:mesh.vertices) {
        const glm::vec3 t(v.tangent);
        if (!Finite(t) || !Finite(v.normal) || glm::dot(t,t)<=1e-12f ||
            (v.tangent.w!=1.0f && v.tangent.w!=-1.0f))
            throw std::runtime_error("glTF TANGENT contains a non-finite, zero or invalid sign value");
        const glm::vec3 ortho=t-v.normal*glm::dot(v.normal,t);
        if (glm::dot(ortho,ortho)<=1e-12f ||
            std::abs(glm::dot(v.normal,glm::normalize(t)))>0.01f)
            throw std::runtime_error("glTF TANGENT must be perpendicular to NORMAL");
        v.tangent=glm::vec4(glm::normalize(ortho),v.tangent.w);
    }
    for (size_t i=0;i<mesh.indices.size();i+=3) {
        const float sign=mesh.vertices[mesh.indices[i]].tangent.w;
        if (sign!=mesh.vertices[mesh.indices[i+1]].tangent.w || sign!=mesh.vertices[mesh.indices[i+2]].tangent.w)
            throw std::runtime_error("glTF triangle has mixed tangent handedness");
    }
}

void GenerateTangents(MeshData& mesh) {
    CheckTopology(mesh);
    for (const Vertex& v:mesh.vertices)
        if (!Finite(v.position) || !Finite(v.normal) || glm::dot(v.normal,v.normal)<0.99f ||
            !std::isfinite(v.texCoord.x) || !std::isfinite(v.texCoord.y))
            throw std::runtime_error("MikkTSpace requires finite positions, unit normals and UVs");
    // 对退化 UV 采用明确的受限导入策略，不让 Mikk 的任意后备基底伪装成可靠结果。
    for (size_t i=0;i<mesh.indices.size();i+=3) {
        const auto p=mesh.vertices[mesh.indices[i]].position;
        FaceNormal(p,mesh.vertices[mesh.indices[i+1]].position,mesh.vertices[mesh.indices[i+2]].position);
        const glm::dvec2 a(mesh.vertices[mesh.indices[i]].texCoord);
        const auto u=glm::dvec2(mesh.vertices[mesh.indices[i+1]].texCoord)-a;
        const auto v=glm::dvec2(mesh.vertices[mesh.indices[i+2]].texCoord)-a;
        const double determinant=u.x*v.y-u.y*v.x;
        const double uvProduct=glm::dot(u,u)*glm::dot(v,v);
        if (uvProduct==0 || determinant*determinant<=uvProduct*1e-12)
            throw std::runtime_error("cannot generate tangents for a degenerate UV triangle");
    }
    MikkInput data{mesh,std::vector<glm::vec4>(mesh.indices.size(),glm::vec4(0))};
    SMikkTSpaceInterface api{};
    api.m_getNumFaces=Faces; api.m_getNumVerticesOfFace=Corners;
    api.m_getPosition=Position; api.m_getNormal=Normal; api.m_getTexCoord=Uv;
    api.m_setTSpaceBasic=SetTangent;
    SMikkTSpaceContext context{&api,&data};
    if (!genTangSpaceDefault(&context)) throw std::runtime_error("MikkTSpace tangent generation failed");
    MeshData output;
    std::map<Key,uint16_t> lookup;
    output.indices.reserve(mesh.indices.size());
    for (size_t i=0;i<mesh.indices.size();++i) {
        const uint16_t source=mesh.indices[i];
        Vertex v=mesh.vertices[source]; v.tangent=data.corners[i];
        output.indices.push_back(AppendCorner(output,lookup,BasisKey(source,v.tangent),v));
    }
    ValidateTangents(output);
    mesh=std::move(output);
}
