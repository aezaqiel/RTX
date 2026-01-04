#include "loader.hpp"

#include <tiny_obj_loader.h>
#include <PathConfig.inl>

namespace {

    std::filesystem::path s_respath(PathConfig::res_dir);

}

auto Loader::load_obj(const std::string& filename) -> Model
{
    std::string path = (s_respath / filename).string();
    std::string base_dir = std::filesystem::path(filename).parent_path().string();

    tinyobj::ObjReaderConfig config;
    config.mtl_search_path = (s_respath / base_dir).string();

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(path, config)) {
        if (!reader.Error().empty()) {
            std::println(std::cerr, "TinObjReader: {}", reader.Error());
        }
    }

    if (!reader.Warning().empty()) {
        std::println(std::cerr, "TinObjReader: {}", reader.Warning());
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    std::vector<glm::vec3> positions;
    std::vector<u32> indices;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    positions.reserve(attrib.vertices.size() / 3);
    normals.reserve(attrib.normals.size() / 3);
    texcoords.reserve(attrib.texcoords.size() / 2);

    for (const auto& shape : shapes) {
        usize offset = 0;
        for (usize f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            u8 fv = shape.mesh.num_face_vertices[f];
            if (fv != 3) {
                offset += fv;
                continue;
            }

            for (usize v = 0; v < 3; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[offset + v];

                positions.push_back({
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                });

                if (idx.normal_index >= 0) {
                    normals.push_back({
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]
                    });
                }

                if (idx.texcoord_index >= 0) {
                    texcoords.push_back({
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        attrib.texcoords[2 * idx.texcoord_index + 1]
                    });
                }

                indices.push_back(static_cast<u32>(indices.size()));
            }

            offset += 3;
        }
    }

    if (normals.size() != positions.size()) normals.clear();
    if (texcoords.size() != positions.size()) texcoords.clear();

    std::println("loaded model: {}, ({} vertices, {} shapes)", filename, positions.size(), shapes.size());

    return Model { .mesh = std::make_unique<Mesh>(positions, indices, normals, texcoords) };
}
