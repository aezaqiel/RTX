#pragma once

#include <glm/glm.hpp>

#include "rhi/buffer.hpp"

struct Mesh
{
    struct VertexAttribute
    {
        glm::vec3 normal;
        glm::vec2 uv;
    };

    std::vector<glm::vec3> positions;
    std::vector<u32> indices;
    std::vector<VertexAttribute> attributes;
};
