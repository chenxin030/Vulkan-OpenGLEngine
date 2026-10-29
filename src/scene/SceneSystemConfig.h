#pragma once

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

enum class SceneSystemKind { Industrial, Animation };

inline const char* SceneSystemName(SceneSystemKind kind)
{
    switch (kind) {
    case SceneSystemKind::Industrial: return "industrial";
    case SceneSystemKind::Animation: return "animation";
    }
    throw std::runtime_error("Unknown scene system kind");
}

inline SceneSystemKind ParseSceneSystem(std::string_view name)
{
    if (name == "industrial") return SceneSystemKind::Industrial;
    if (name == "animation") return SceneSystemKind::Animation;
    throw std::runtime_error("Unknown scene system: " + std::string(name));
}

// 顺序就是注册/执行顺序；工业系统目前依赖随后执行的动画与世界变换求解。
inline void ValidateSceneSystems(std::span<const SceneSystemKind> systems)
{
    bool industrial = false, animation = false;
    for (auto kind : systems) {
        switch (kind) {
        case SceneSystemKind::Industrial:
            if (industrial) throw std::runtime_error("Duplicate scene system: industrial");
            if (animation) throw std::runtime_error("industrial must precede animation");
            industrial = true;
            break;
        case SceneSystemKind::Animation:
            if (animation) throw std::runtime_error("Duplicate scene system: animation");
            animation = true;
            break;
        default: throw std::runtime_error("Unknown scene system kind");
        }
    }
    if (industrial && !animation)
        throw std::runtime_error("industrial requires animation after it");
}
