#include <cyuediff/vertex_attributes.hpp>
#include <cyuediff/string_encoding.hpp>
#include <cmath>
#include <cstring>
namespace cyuediff {
namespace {
unsigned width(VertexAttributeFormat format) {
    switch (format) {
    case VertexAttributeFormat::float32:
        return 4;
    case VertexAttributeFormat::float16:
        return 2;
    case VertexAttributeFormat::unorm8:
        return 1;
    }
    throw Error("mesh-attribute-format", "Unknown stored vertex attribute encoding");
}
void need(bool value, const char *message) {
    if (!value)
        throw Error("mesh-attribute-layout", message);
}
} // namespace
MeshVertexAttribute::MeshVertexAttribute(std::string name, VertexAttributeFormat format,
                                         std::uint32_t components, std::vector<std::uint8_t> bytes,
                                         const VertexAttributeOptions &options)
    : name_(std::move(name)), format_(format), components_(components), bytes_(std::move(bytes)) {
    options.control.check();
    need(!name_.empty() && name_.find('\0') == std::string::npos, "Empty or invalid vertex attribute name");
    need(components >= 1 && components <= 4, "Vertex attributes require one to four components");
    if (name_.size() > options.max_name_bytes || name_.size() > options.max_output_bytes)
        throw Error("mesh-attribute-limit", "Vertex attribute name exceeds budget");
    name_ = decode_narrow_string(name_, NarrowStringEncoding::utf8, options.max_name_bytes, options.control);
    const auto stride = width(format) * components;
    need(bytes_.size() % stride == 0, "Vertex attribute byte count disagrees with its components");
    const auto count = bytes_.size() / stride;
    if (count > options.max_vertices || bytes_.size() > options.max_output_bytes - name_.size() ||
        count * (std::uint64_t(components) * 64 + 128) >
            options.max_output_bytes - name_.size() - bytes_.size())
        throw Error("mesh-attribute-limit", "Vertex attribute decoded output exceeds budget");
    vertices_ = static_cast<std::uint32_t>(count);
    for (std::uint64_t i = 0; i < std::uint64_t(vertices_) * components_; ++i) {
        if ((i & 4095) == 0)
            options.control.check();
        need(std::isfinite(component(i)), "Non-finite vertex attribute value");
    }
    sha256_ = sha256_bytes(bytes_, options.control);
}
float MeshVertexAttribute::component(std::uint64_t index) const {
    const auto size = width(format_);
    const auto at = index * size;
    need(at <= bytes_.size() && size <= bytes_.size() - at, "Vertex attribute component exceeds buffer");
    std::uint32_t bits = 0;
    for (unsigned i = 0; i < size; ++i)
        bits |= std::uint32_t(bytes_[static_cast<std::size_t>(at + i)]) << (8 * i);
    if (format_ == VertexAttributeFormat::unorm8)
        return static_cast<float>(bits) / 255.f;
    if (format_ == VertexAttributeFormat::float32) {
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    const auto exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    need(exponent != 31, "Non-finite half vertex attribute value");
    return (bits & 32768 ? -1.f : 1.f) * std::ldexp(static_cast<float>(exponent ? mantissa + 1024 : mantissa),
                                                    exponent ? int(exponent) - 25 : -24);
}
std::string MeshVertexAttribute::encoding() const {
    switch (format_) {
    case VertexAttributeFormat::float32:
        return "float32";
    case VertexAttributeFormat::float16:
        return "float16";
    case VertexAttributeFormat::unorm8:
        return "unorm8";
    }
    throw Error("mesh-attribute-format", "Unknown stored vertex attribute encoding");
}
std::vector<float> MeshVertexAttribute::vertex(std::uint32_t id, const OperationControl &control) const {
    control.check();
    if (id >= vertices_)
        throw Error("mesh-attribute-selection", "Vertex attribute has no selected render vertex");
    std::vector<float> values;
    for (unsigned i = 0; i < components_; ++i)
        values.push_back(component(std::uint64_t(id) * components_ + i));
    return values;
}
Json MeshVertexAttribute::semantic_snapshot(const OperationControl &control) const {
    control.check();
    Json values = Json::object();
    for (std::uint32_t vi = 0; vi < vertices_; ++vi) {
        control.check();
        values[std::to_string(vi)] = vertex(vi, control);
    }
    return {{"components", components_}, {"vertices", std::move(values)}};
}
Json MeshVertexAttribute::to_json(bool include_values, const OperationControl &control) const {
    control.check();
    Json out = {{"name", name_},
                {"encoding", encoding()},
                {"components", components_},
                {"vertices", vertices_},
                {"bytes", bytes_.size()},
                {"sha256", sha256_},
                {"scope", "stored-render-vertex-attribute"},
                {"runtimeEvaluation", false}};
    if (include_values) {
        out["values"] = Json::array();
        for (std::uint32_t vi = 0; vi < vertices_; ++vi) {
            control.check();
            out["values"].push_back(vertex(vi, control));
        }
    }
    return out;
}
Json MeshVertexAttributes::semantic_snapshot(const OperationControl &control) const {
    control.check();
    Json values = Json::object();
    for (const auto &entry : channels) {
        control.check();
        need(entry.second && entry.first == entry.second->name(),
             "Invalid vertex attribute channel identity");
        values[entry.first] = entry.second->semantic_snapshot(control);
    }
    return {{"schema", "cyuediff.vertex-attributes/1"},
            {"scope", "stored-render-vertex-attributes"},
            {"channels", std::move(values)}};
}
Json MeshVertexAttributes::to_json(bool include_values, const OperationControl &control) const {
    control.check();
    Json values = Json::array();
    for (const auto &entry : channels) {
        control.check();
        need(entry.second && entry.first == entry.second->name(),
             "Invalid vertex attribute channel identity");
        values.push_back(entry.second->to_json(include_values, control));
    }
    return {{"schema", "cyuediff.vertex-attributes/1"},
            {"scope", "stored-render-vertex-attributes"},
            {"channels", std::move(values)}};
}
} // namespace cyuediff
