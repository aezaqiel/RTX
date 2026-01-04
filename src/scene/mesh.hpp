#pragma once

#include <glm/glm.hpp>

struct Mesh
{
    std::vector<glm::vec3> p;
    std::vector<glm::vec3> n;
    std::vector<glm::vec2> uv;
    std::vector<u32> indices;

    Mesh() = default;
    Mesh(
        const std::vector<glm::vec3>& positions,
        const std::vector<u32>& indices,
        const std::vector<glm::vec3>& normals = {},
        const std::vector<glm::vec2>& uvs = {}
    )
        : p(positions), indices(indices), n(normals), uv(uvs)
    {
    }
};
