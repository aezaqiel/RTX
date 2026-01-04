#pragma once

#include "mesh.hpp"

struct Model
{
    std::unique_ptr<Mesh> mesh;
    // TODO: material
};
