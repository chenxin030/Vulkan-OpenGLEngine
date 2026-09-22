#pragma once

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// The vertex format used by scene meshes.
//
// Deliberately backend-neutral: it says what a vertex IS, not how a graphics API
// wants it described. The Vulkan-specific binding and attribute descriptions are
// built from this inside the backend, because stride, attribute location and
// VkFormat are API concepts, not scene concepts.
//
// That is the whole reason this file sits under scene/ and not under rhi/. The
// split looks pedantic until you need a second backend or an offline mesh
// importer - then it is the difference between reusing this type and copying it.
// ---------------------------------------------------------------------------
struct Vertex
{
    glm::vec3 position;
    glm::vec3 color;

    // Declared and interpolated already, but nothing samples it yet; the texture
    // work in Step C starts consuming it for real. Keeping the field means the
    // vertex layout, pipeline and shader do not all have to change at once later.
    glm::vec2 texCoord;
};
