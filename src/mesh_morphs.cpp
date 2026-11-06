#include <cyuediff/mesh_morphs.hpp>
#include <cmath>
#include <set>
namespace cyuediff {
namespace {
std::string key(std::string text) {
    for (auto &c : text)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return text;
}
void validate_targets(const MeshMorphs &morphs, const OperationControl &control) {
    std::set<std::string> names;
    for (const auto &target : morphs.targets) {
        control.check();
        const auto name = key(target.first);
        if (name.empty() || name == "none" || target.first.find('\0') != std::string::npos ||
            !names.insert(name).second)
            throw Error("mesh-morph-identity", "Empty or ambiguous source morph target name");
        if (!target.second.normals_present && !target.second.normals.empty())
            throw Error("mesh-morph-layout", "Normal deltas require a stored normal attribute");
        for (const auto *table : {&target.second.positions, &target.second.normals})
            for (const auto &delta : *table) {
                control.check();
                for (const auto value : delta.second)
                    if (!std::isfinite(value))
                        throw Error("mesh-morph-number", "Non-finite source morph delta");
            }
    }
}
} // namespace
void MeshMorphs::validate(const MeshGeometry &mesh, const OperationControl &control) const {
    validate_targets(*this, control);
    std::set<std::uint32_t> vertices, instances;
    for (const auto &v : mesh.vertices) {
        control.check();
        if (!vertices.insert(v.id).second)
            throw Error("mesh-morph-reference", "Duplicate source vertex ID");
    }
    for (const auto &i : mesh.instances) {
        control.check();
        if (!vertices.count(i.vertex) || !instances.insert(i.id).second)
            throw Error("mesh-morph-reference", "Invalid source vertex instance");
    }
    for (const auto &target : targets) {
        const auto &m = target.second;
        if (m.positions.size() != vertices.size() ||
            (m.normals_present && m.normals.size() != instances.size()))
            throw Error("mesh-morph-reference", "Morph deltas must cover the live source elements exactly");
        for (const auto &p : m.positions) {
            control.check();
            if (!vertices.count(p.first))
                throw Error("mesh-morph-reference", "Morph delta references an absent vertex");
        }
        for (const auto &n : m.normals) {
            control.check();
            if (!instances.count(n.first))
                throw Error("mesh-morph-reference", "Morph delta references an absent vertex instance");
        }
    }
}
Json MeshMorphs::to_json() const {
    validate_targets(*this, {});
    Json out = Json::object();
    for (const auto &target : targets) {
        Json positions = Json::object(), normals = Json::object();
        for (const auto &item : target.second.positions)
            positions[std::to_string(item.first)] = item.second;
        for (const auto &item : target.second.normals)
            normals[std::to_string(item.first)] = item.second;
        out[target.first] = {{"positions", positions},
                             {"normals", target.second.normals_present ? normals : Json(nullptr)}};
    }
    return {{"schema", "cyuediff.mesh-morphs/1"}, {"targets", out}};
}
} // namespace cyuediff
