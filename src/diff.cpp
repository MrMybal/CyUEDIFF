#include <cyuediff/core.hpp>
namespace cyuediff {
namespace {
std::string pointer_token(const std::string &input) {
    std::string out;
    for (char c : input) {
        if (c == '~')
            out += "~0";
        else if (c == '/')
            out += "~1";
        else
            out += c;
    }
    return out;
}
void walk(const Json &before, const Json &after, const std::string &path, Json &changes) {
    if (before == after)
        return;
    if (before.is_object() && after.is_object()) {
        for (auto it = before.begin(); it != before.end(); ++it) {
            auto child = path + "/" + pointer_token(it.key());
            if (!after.contains(it.key()))
                changes.push_back({{"op", "remove"}, {"path", child}, {"before", it.value()}});
            else
                walk(it.value(), after.at(it.key()), child, changes);
        }
        for (auto it = after.begin(); it != after.end(); ++it)
            if (!before.contains(it.key()))
                changes.push_back(
                    {{"op", "add"}, {"path", path + "/" + pointer_token(it.key())}, {"after", it.value()}});
    } else if (before.is_array() && after.is_array() && before.size() == after.size()) {
        for (std::size_t i = 0; i < before.size(); ++i)
            walk(before[i], after[i], path + "/" + std::to_string(i), changes);
    } else
        changes.push_back({{"op", "replace"}, {"path", path}, {"before", before}, {"after", after}});
}
} // namespace
std::string DiffResult::status() const {
    if (!complete_input)
        return "incomplete";
    if (!changes.empty() || !graph_changes.empty() || !texture_changes.empty() || !mesh_changes.empty() ||
        !material_changes.empty())
        return "changed";
    if (!binary_comparison_available)
        return "unverified";
    return binary_changed ? "binary-only" : "identical";
}
Json DiffResult::to_json() const {
    return {{"schema", "cyuediff.diff/1"},
            {"status", status()},
            {"binaryComparisonAvailable", binary_comparison_available},
            {"binaryChanged", binary_comparison_available ? Json(binary_changed) : Json(nullptr)},
            {"completeInput", complete_input},
            {"changes", changes},
            {"graphChanges", graph_changes},
            {"textureChanges", texture_changes},
            {"meshChanges", mesh_changes},
            {"materialChanges", material_changes},
            {"diagnostics", diagnostics}};
}
std::string MaterialComparison::status() const {
    return !complete ? "incomplete" : (changes.empty() ? "identical" : "changed");
}
Json MaterialComparison::to_json() const {
    return {{"schema", "cyuediff.material-diff/1"},
            {"status", status()},
            {"complete", complete},
            {"scope", "serialized material graph and stored instance parameters"},
            {"changes", changes}};
}
MaterialComparison compare_materials(const Package &before, const Package &after) {
    auto a = before.material_snapshot(), b = after.material_snapshot();
    auto complete = [](const Json &model) {
        if (model.at("graphs").empty() && model.at("instances").empty())
            return false;
        for (const auto *kind : {"graphs", "instances"})
            for (const auto &entry : model.at(kind))
                if (!entry.value("complete", false))
                    return false;
        return true;
    };
    MaterialComparison result;
    result.complete = before.complete_input && after.complete_input && complete(a) && complete(b);
    walk(a, b, "", result.changes);
    return result;
}
MeshComparison compare_meshes(const MeshGeometry &before, const MeshGeometry &after) {
    MeshComparison result;
    walk(before.to_json(), after.to_json(), "", result.changes);
    return result;
}
DiffResult compare(const Package &before, const Package &after) {
    DiffResult result;
    result.complete_input = before.complete_input && after.complete_input;
    result.binary_comparison_available = !before.fingerprints.empty() && !after.fingerprints.empty();
    if (result.binary_comparison_available)
        result.binary_changed = before.fingerprints != after.fingerprints;
    walk(before.semantic_snapshot(), after.semantic_snapshot(), "", result.changes);
    walk(before.blueprint_snapshot(), after.blueprint_snapshot(), "", result.graph_changes);
    walk(before.texture_snapshot(), after.texture_snapshot(), "", result.texture_changes);
    walk(before.mesh_snapshot(), after.mesh_snapshot(), "", result.mesh_changes);
    walk(before.material_snapshot(), after.material_snapshot(), "", result.material_changes);
    result.diagnostics.push_back(
        "Opaque hashes identify changed bytes. Texture source metadata is projected separately; "
        "pixel comparison is available through image-diff for supported sources. "
        "Mesh geometry comparison is explicit through mesh-diff; Blueprint bytecode remains opaque.");
    if (!before.diagnostics.empty() || !after.diagnostics.empty()) {
        result.diagnostics.push_back("Inspect source package diagnostics for per-export decoding limits.");
    }
    if (!result.complete_input)
        result.diagnostics.push_back(
            "A required export payload is unavailable; this comparison cannot establish equality.");
    return result;
}
} // namespace cyuediff
