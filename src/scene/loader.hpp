#pragma once

#include "model.hpp"

class Loader
{
public:
    static auto load_obj(const std::string& filename) -> Model;
};
