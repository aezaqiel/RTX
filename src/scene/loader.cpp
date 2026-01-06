#include "loader.hpp"

#include <tiny_obj_loader.h>
#include <meshoptimizer.h>

#include <pathconfig.inl>

namespace {

    std::filesystem::path s_respath(PathConfig::res_dir);

}

auto Loader::load_obj(const std::string& filename) -> Model
{
    std::println("loading {}", filename);

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

    std::vector<Vertex> raw_vertices;
    raw_vertices.reserve(attrib.vertices.size() / 3);

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

                Vertex vertex;

                vertex.position = {
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                };

                if (idx.normal_index >= 0) {
                    vertex.normal = {
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]
                    };
                }

                if (idx.texcoord_index >= 0) {
                    vertex.uv = {
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        attrib.texcoords[2 * idx.texcoord_index + 1]
                    };
                }

                raw_vertices.push_back(vertex);
            }

            offset += 3;
        }
    }

    std::println(" - raw data points: {}", raw_vertices.size());

    usize index_count = raw_vertices.size();
    std::vector<u32> remap(index_count);

    usize vertex_count = meshopt_generateVertexRemap( remap.data(), nullptr, index_count, raw_vertices.data(), index_count, sizeof(Vertex));

    std::vector<u32> indices(index_count);
    std::vector<Vertex> unique_vertices(vertex_count);

    meshopt_remapIndexBuffer(indices.data(), nullptr, index_count, remap.data());
    meshopt_remapVertexBuffer(unique_vertices.data(), raw_vertices.data(), index_count, sizeof(Vertex), remap.data());

    meshopt_optimizeVertexCache(indices.data(), indices.data(), index_count, vertex_count);

    std::vector<Vertex> vertices(vertex_count);

    meshopt_optimizeVertexFetch(vertices.data(), indices.data(), index_count, unique_vertices.data(), vertex_count, sizeof(Vertex));

    auto mesh = std::make_unique<Mesh>();
    mesh->indices = std::move(indices);
    mesh->vertices = std::move(vertices);

    std::println(" - vertices: {}", mesh->vertices.size());
    std::println(" - triangles: {}", mesh->indices.size() / 3);

    return Model { .mesh = std::move(mesh) };
}
