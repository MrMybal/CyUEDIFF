#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
enum class VertexAttributeFormat { float32, float16, unorm8 };
struct VertexAttributeOptions {
    std::uint32_t max_vertices = 1'000'000, max_name_bytes = 4096;
    std::uint64_t max_output_bytes = 512ull * 1024 * 1024;
    OperationControl control;
};
// Immutable finite stored render values. Encoding remains available separately;
// no shader interpretation, interpolation or runtime deformation is inferred.
class MeshVertexAttribute {
    std::string name_, sha256_;
    VertexAttributeFormat format_;
    std::uint32_t components_ = 0, vertices_ = 0;
    std::vector<std::uint8_t> bytes_;
    float component(std::uint64_t index) const;

  public:
    MeshVertexAttribute(std::string name, VertexAttributeFormat, std::uint32_t components,
                        std::vector<std::uint8_t> bytes, const VertexAttributeOptions & = {});
    const std::string &name() const {
        return name_;
    }
    VertexAttributeFormat format() const {
        return format_;
    }
    std::uint32_t components() const {
        return components_;
    }
    std::uint32_t vertex_count() const {
        return vertices_;
    }
    const std::vector<std::uint8_t> &encoded() const {
        return bytes_;
    }
    std::string encoding() const;
    std::vector<float> vertex(std::uint32_t id, const OperationControl & = {}) const;
    Json semantic_snapshot(const OperationControl & = {}) const;
    Json to_json(bool include_values = false, const OperationControl & = {}) const;
};
struct MeshVertexAttributes {
    std::map<std::string, std::shared_ptr<const MeshVertexAttribute>> channels;
    Json semantic_snapshot(const OperationControl & = {}) const;
    Json to_json(bool include_values = false, const OperationControl & = {}) const;
};
} // namespace cyuediff
