#pragma once

#include "scene/Scene.h"
#include "scene/SceneSystem.h"
#include <vector>

// 在 AnimationSystem 之前注册，先修改机器 local，再于世界变换求解后派发货物。
class IndustrialSystem : public SceneSystem
{
public:
    void Start(SceneSystemContext& context) override;
    void FixedUpdate(SceneSystemContext& context, double deltaSeconds) override;
    void AfterFixedUpdate(SceneSystemContext& context, double deltaSeconds) override;
    void AppendStatistics(std::vector<RuntimeStatistic>& statistics) const override;
private:
    struct Motion { 
        Entity entity; 
        glm::mat4 base; 
    };
    struct Instance {
        std::vector<Entity> nodes; // 先父级，包括根和子节点
        glm::vec3 start{}, end{};
        uint64_t born = 0, duration = 1;
    };
    struct Route {
        Entity entity;
        std::vector<uint32_t> idle;
        uint64_t next = 0;
    };
    std::vector<Entity> m_activeNodes;
    std::vector<Motion> m_motions;
    std::vector<Instance> m_instances;
    std::vector<Route> m_routes;
    std::vector<uint32_t> m_active;
    std::vector<uint32_t> m_owner;
};

void ValidateIndustrialRules(const Scene& scene);
