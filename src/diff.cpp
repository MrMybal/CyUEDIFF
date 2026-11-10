#include <cyuediff/render_morphs.hpp>
#include <cyuediff/curves.hpp>
#include <cyuediff/animation.hpp>
#include <cyuediff/core.hpp>
#include <cyuediff/mesh_overlaps.hpp>
#include <cyuediff/vertex_attributes.hpp>
#include <cyuediff/collision.hpp>
#include <cyuediff/skeleton.hpp>
#include <cyuediff/attachments.hpp>
#include "source_availability.hpp"
#include "name_value_diff.hpp"
#include <algorithm>
#include "json_equality.hpp"
#include "json_output.hpp"
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
void walk(const Json &before, const Json &after, const std::string &path, Json &changes,
          const OperationControl &control = {}, const detail::SourceCoverage *coverage = nullptr) {
    control.check();
    if ((coverage && coverage->excludes(path)) || detail::json_equal(before, after, control))
        return;
    if (before.is_object() && after.is_object()) {
        for (auto it = before.begin(); it != before.end(); ++it) {
            auto child = path + "/" + pointer_token(it.key());
            if (coverage && coverage->excludes(child))
                continue;

            if (!after.contains(it.key()))
                changes.push_back({{"op", "remove"}, {"path", child}, {"before", it.value()}});
            else
                walk(it.value(), after.at(it.key()), child, changes, control, coverage);
        }
        for (auto it = after.begin(); it != after.end(); ++it)
            if (!before.contains(it.key()) &&
                !(coverage && coverage->excludes(path + "/" + pointer_token(it.key()))))
                changes.push_back(
                    {{"op", "add"}, {"path", path + "/" + pointer_token(it.key())}, {"after", it.value()}});
    } else if (before.is_array() && after.is_array() &&
               (before.size() == after.size() || (coverage && coverage->intersects(path)))) {
        for (std::size_t i = 0; i < std::min(before.size(), after.size()); ++i)
            walk(before[i], after[i], path + "/" + std::to_string(i), changes, control, coverage);
        for (std::size_t i = after.size(); i < before.size(); ++i) {
            control.check();
            auto child = path + "/" + std::to_string(i);
            if (!coverage || !coverage->excludes(child))
                changes.push_back({{"op", "remove"}, {"path", child}, {"before", before[i]}});
        }
        for (std::size_t i = before.size(); i < after.size(); ++i) {
            control.check();
            auto child = path + "/" + std::to_string(i);
            if (!coverage || !coverage->excludes(child))
                changes.push_back({{"op", "add"}, {"path", child}, {"after", after[i]}});
        }
    } else
        changes.push_back({{"op", "replace"}, {"path", path}, {"before", before}, {"after", after}});
}
// The snapshot contract is retained, but matched export payloads are borrowed.
Json object_metadata(const Package &p, const ObjectExport &e) {
    return {{"class", e.class_name},
            {"category", e.category},
            {"flags", e.flags},
            {"super", p.reference(e.super_index)},
            {"template", p.reference(e.template_index)}};
}
Json object_snapshot(const Package &p, const ObjectExport &e) {
    auto value = object_metadata(p, e);
    value["properties"] = e.properties;
    value["opaque"] = e.opaque;
    if (!e.native_data.empty())
        value["native"] = e.native_data;
    return value;
}
Json dependencies(const Package &p, const OperationControl &control) {
    Json out = Json::object();
    for (std::size_t i = 0; i < p.imports.size(); ++i) {
        control.check();
        auto key = p.reference(-static_cast<int>(i) - 1);
        if (!out.contains(key))
            out[key] = Json::array();
        const auto &entry = p.imports[i];
        out[key].push_back({{"class", entry.class_name},
                            {"classPackage", entry.class_package},
                            {"package", entry.package_name}});
    }
    for (auto &entries : out) {
        control.check();
        std::sort(entries.begin(), entries.end());
    }
    return out;
}
void compare_objects(const Package &before, const Package &after, Json &changes,
                     const detail::SourceCoverage &coverage, const OperationControl &control) {
    std::map<std::string, const ObjectExport *> a, b;
    for (const auto &entry : before.exports) {
        control.check();
        a.emplace(entry.path, &entry);
    }
    for (const auto &entry : after.exports) {
        control.check();
        b.emplace(entry.path, &entry);
    }
    for (const auto &entry : a) {
        control.check();
        const auto path = "/objects/" + pointer_token(entry.first);
        if (coverage.excludes(path))
            continue;
        auto peer = b.find(entry.first);
        if (peer == b.end()) {
            changes.push_back(
                {{"op", "remove"}, {"path", path}, {"before", object_snapshot(before, *entry.second)}});
            continue;
        }
        auto x = object_metadata(before, *entry.second), y = object_metadata(after, *peer->second);
        using Fields = std::map<std::string, const Json *>;
        Fields left, right;
        auto fields = [](const ObjectExport &e, const Json &metadata, Fields &out) {
            for (auto i = metadata.begin(); i != metadata.end(); ++i)
                out.emplace(i.key(), &i.value());
            out.emplace("properties", &e.properties);
            out.emplace("opaque", &e.opaque);
            if (!e.native_data.empty())
                out.emplace("native", &e.native_data);
        };
        fields(*entry.second, x, left);
        fields(*peer->second, y, right);
        for (const auto &field : left) {
            const auto child = path + "/" + field.first;
            if (coverage.excludes(child))
                continue;
            auto other = right.find(field.first);
            if (other == right.end())
                changes.push_back({{"op", "remove"}, {"path", child}, {"before", *field.second}});
            else
                walk(*field.second, *other->second, child, changes, control, &coverage);
        }
        for (const auto &field : right)
            if (!left.count(field.first)) {
                auto child = path + "/" + field.first;
                if (!coverage.excludes(child))
                    changes.push_back({{"op", "add"}, {"path", child}, {"after", *field.second}});
            }
    }
    for (const auto &entry : b)
        if (!a.count(entry.first)) {
            control.check();
            auto path = "/objects/" + pointer_token(entry.first);
            if (!coverage.excludes(path))
                changes.push_back(
                    {{"op", "add"}, {"path", path}, {"after", object_snapshot(after, *entry.second)}});
        }
}
void append_projection(const Json &rows, const char *prefix, Json &changes, const OperationControl &control) {
    for (const auto &row : rows) {
        control.check();
        auto value = row;
        value["path"] = std::string(prefix) + row.at("path").get<std::string>();
        changes.push_back(std::move(value));
    }
}
detail::SourceCoverage source_projection_coverage(const Package &before, const Package &after, const Json &a,
                                                  const Json &b, const std::string &domain,
                                                  const OperationControl &control = {}) {
    control.check();
    if (before.source_read_limit_failed || after.source_read_limit_failed) {
        detail::SourceCoverage result;
        result.suppressed.insert("");
        result.unavailable.push_back({{"domain", domain}, {"path", ""}, {"reason", "source-read-limits"}});
        return result;
    }
    detail::SourceCoverageOptions options;
    options.control = control;
    return detail::projection_coverage(detail::source_coverage(before, after, options), before, after, a, b,
                                       domain, control);
}
} // namespace
std::string CurveComparison::status() const {
    return !complete ? "incomplete" : changes.empty() ? "identical" : "changed";
}
Json CurveComparison::to_json() const {
    return {{"schema", "cyuediff.curves-diff/1"},
            {"scope", "selected stored rich-curve keys and settings"},
            {"beforeObject", before_object},
            {"afterObject", after_object},
            {"complete", complete},
            {"status", status()},
            {"changes", changes}};
}
CurveComparison compare_curves(const Package &before, const Package &after, const std::string &before_object,
                               const std::string &after_object, const CurveOptions &o) {
    auto options = o;
    options.source_indices = false;
    const auto a = curve_snapshot(before, options), b = curve_snapshot(after, options);
    auto select = [](const Json &root, const std::string &path) {
        const auto &owners = root.at("owners");
        if (path.empty() && owners.size() == 1)
            return owners.begin().key();
        if (path.empty() || !owners.contains(path))
            throw Error("curve-object", "Select exactly one stored curve owner");
        return path;
    };
    CurveComparison out;
    out.before_object = select(a, before_object);
    out.after_object = select(b, after_object);
    const auto &x = a["owners"][out.before_object], &y = b["owners"][out.after_object];
    out.complete = before.complete_input && after.complete_input && x.at("complete").get<bool>() &&
                   y.at("complete").get<bool>();
    walk(x, y, "", out.changes, o.control);
    return out;
}
std::string AnimationComparison::status() const {
    return !complete ? "incomplete" : changes.empty() ? "identical" : "changed";
}
Json AnimationComparison::to_json() const {
    return {{"schema", "cyuediff.animation-diff/1"},
            {"scope", "selected stored AnimDataModel bone key streams"},
            {"beforeObject", before_object},
            {"afterObject", after_object},
            {"complete", complete},
            {"status", status()},
            {"changes", changes}};
}
AnimationComparison compare_animation_models(const Package &before, const Package &after,
                                             const std::string &before_object,
                                             const std::string &after_object,
                                             const AnimationOptions &options) {
    auto normalized = options;
    normalized.source_indices = false;
    const auto a = animation_snapshot(before, normalized), b = animation_snapshot(after, normalized);
    auto select = [](const Json &root, const std::string &path) -> std::string {
        const auto &models = root.at("models");
        if (path.empty()) {
            if (models.size() != 1)
                throw Error("animation-object", "Select exactly one stored animation model");
            return models.begin().key();
        }
        if (!models.contains(path))
            throw Error("animation-object", "Animation model not found");
        return path;
    };
    AnimationComparison out;
    out.before_object = select(a, before_object);
    out.after_object = select(b, after_object);
    const auto &x = a["models"][out.before_object], &y = b["models"][out.after_object];
    out.complete = before.complete_input && after.complete_input && x.at("complete").get<bool>() &&
                   y.at("complete").get<bool>();
    walk(x, y, "", out.changes, options.control);
    return out;
}
std::string DiffResult::status() const {
    if (!complete_input)
        return "incomplete";
    if (source_read_limit_failed || !unavailable_scopes.empty())
        return "unverified";
    if (!changes.empty() || !graph_changes.empty() || !texture_changes.empty() || !mesh_changes.empty() ||
        !material_changes.empty() || !collision_changes.empty() || !skeleton_changes.empty())
        return "changed";
    if (!binary_comparison_available)
        return "unverified";
    return binary_changed ? "binary-only" : "identical";
}
namespace {
Json diff_header(const DiffResult &d) {
    Json result = {{"schema", "cyuediff.diff/1"},
                   {"status", d.status()},
                   {"binaryComparisonAvailable", d.binary_comparison_available},
                   {"binaryChanged", d.binary_comparison_available ? Json(d.binary_changed) : Json(nullptr)},
                   {"completeInput", d.complete_input},
                   {"projectionComparisonComplete",
                    d.complete_input && !d.source_read_limit_failed && d.unavailable_scopes.empty()},
                   {"diagnostics", d.diagnostics}};
    if (d.source_read_limit_failed)
        result["sourceReadLimitFailed"] = true;
    return result;
}
} // namespace
detail::JsonObject detail::diff_report(const DiffResult &d) {
    JsonObject result;
    auto header = diff_header(d);
    for (auto i = header.begin(); i != header.end(); ++i)
        result.own(i.key(), std::move(i.value()));
    result.borrow("changes", d.changes);
    result.borrow("graphChanges", d.graph_changes);
    result.borrow("textureChanges", d.texture_changes);
    result.borrow("meshChanges", d.mesh_changes);
    result.borrow("materialChanges", d.material_changes);
    result.borrow("collisionChanges", d.collision_changes);
    result.borrow("skeletonChanges", d.skeleton_changes);
    if (!d.unavailable_scopes.empty())
        result.borrow("unavailableScopes", d.unavailable_scopes);
    if (!d.source_contexts.is_null())
        result.borrow("sourceContexts", d.source_contexts);
    return result;
}
Json DiffResult::to_json() const { return detail::diff_report(*this).to_json(); }
void DiffResult::write_json(std::ostream &stream, int indent, const Json &additional,
                            const OperationControl &control) const {
    if (!additional.is_object())
        throw Error("report-field", "Additional report fields must be an object");
    static const std::set<std::string> reserved = {"schema",
                                                   "status",
                                                   "binaryComparisonAvailable",
                                                   "binaryChanged",
                                                   "completeInput",
                                                   "projectionComparisonComplete",
                                                   "diagnostics",
                                                   "sourceReadLimitFailed",
                                                   "changes",
                                                   "graphChanges",
                                                   "textureChanges",
                                                   "meshChanges",
                                                   "materialChanges",
                                                   "collisionChanges",
                                                   "skeletonChanges",
                                                   "unavailableScopes",
                                                   "sourceContexts"};
    auto report = detail::diff_report(*this);
    for (auto i = additional.begin(); i != additional.end(); ++i) {
        if (reserved.count(i.key()))
            throw Error("report-field", "Additional fields cannot override the core report contract");
        report.borrow(i.key(), i.value());
    }
    report.write(stream, indent, control);
}

std::string MaterialComparison::status() const {
    return !complete ? "incomplete" : (changes.empty() ? "identical" : "changed");
}
Json MaterialComparison::to_json() const {
    Json result = {{"schema", "cyuediff.material-diff/1"},
                   {"status", status()},
                   {"complete", complete},
                   {"scope", "serialized material graph and stored instance parameters"},
                   {"changes", changes}};
    if (!unavailable_scopes.empty())
        result["unavailableScopes"] = unavailable_scopes;
    return result;
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
    auto coverage = source_projection_coverage(before, after, a, b, "materials");
    result.unavailable_scopes = coverage.unavailable;
    result.complete = result.complete && coverage.unavailable.empty();
    walk(a, b, "", result.changes, {}, &coverage);
    return result;
}
std::string CollisionComparison::status() const {
    return !complete ? "incomplete" : changes.empty() ? "identical" : "changed";
}
Json CollisionComparison::to_json() const {
    Json result = {{"schema", "cyuediff.collision-diff/1"},
                   {"status", status()},
                   {"complete", complete},
                   {"scope", "stored reflected collision shapes and body references"},
                   {"changes", changes}};
    if (!unavailable_scopes.empty())
        result["unavailableScopes"] = unavailable_scopes;
    return result;
}
CollisionComparison compare_collisions(const Package &before, const Package &after,
                                       const CollisionOptions &options) {
    auto normalized = options;
    normalized.source_indices = false;
    const auto a = collision_snapshot(before, normalized), b = collision_snapshot(after, normalized);
    CollisionComparison result;
    result.complete = a.at("complete").get<bool>() && b.at("complete").get<bool>();
    auto coverage = source_projection_coverage(before, after, a, b, "collisions", options.control);
    result.unavailable_scopes = coverage.unavailable;
    result.complete = result.complete && coverage.unavailable.empty();
    walk(a, b, "", result.changes, options.control, &coverage);
    return result;
}
CollisionComparison compare_collisions(const ZenPackage &before, const ZenPackage &after,
                                       const CollisionOptions &options) {
    auto normalized = options;
    normalized.source_indices = false;
    auto a = collision_snapshot(before, normalized), b = collision_snapshot(after, normalized);
    a.erase("provenance");
    b.erase("provenance");
    CollisionComparison result;
    result.complete = a.at("complete").get<bool>() && b.at("complete").get<bool>();
    walk(a, b, "", result.changes, options.control);
    return result;
}
std::string AttachmentComparison::status() const {
    return !complete ? "incomplete" : changes.empty() ? "identical" : "changed";
}
Json AttachmentComparison::to_json() const {
    return {{"schema", "cyuediff.attachments-diff/1"},
            {"scope", "stored skeletal sockets, explicit membership and virtual-bone definitions; no "
                      "defaults or external loading"},
            {"status", status()},
            {"complete", complete},
            {"changes", changes}};
}
AttachmentComparison compare_attachments(const Package &before, const Package &after,
                                         const AttachmentOptions &options) {
    auto normalized = options;
    normalized.source_indices = false;
    const auto a = attachment_snapshot(before, normalized), b = attachment_snapshot(after, normalized);
    AttachmentComparison out;
    out.complete = a.at("complete").get<bool>() && b.at("complete").get<bool>();
    walk(a, b, "", out.changes, options.control);
    return out;
}
std::string SkeletonAnimationComparison::status() const {
    return !complete ? "incomplete" : changes.empty() ? "identical" : "changed";
}
Json SkeletonAnimationComparison::to_json() const {
    return {{"schema", "cyuediff.skeleton-animation-diff/1"},
            {"scope", "stored Skeleton curve metadata and sync marker names"},
            {"status", status()},
            {"complete", complete},
            {"changes", changes}};
}
SkeletonAnimationComparison compare_skeleton_animation(const Package &before, const Package &after,
                                                       const SkeletonOptions &options) {
    const auto a = skeleton_animation_snapshot(before, options),
               b = skeleton_animation_snapshot(after, options);
    SkeletonAnimationComparison result;
    result.complete = a.at("complete").get<bool>() && b.at("complete").get<bool>();
    walk(a, b, "", result.changes, options.control);
    return result;
}
std::string SkeletonComparison::status() const {
    return !complete ? "incomplete" : changes.empty() ? "identical" : "changed";
}
Json SkeletonComparison::to_json() const {
    Json result = {{"schema", "cyuediff.skeleton-diff/1"},
                   {"complete", complete},
                   {"status", status()},
                   {"scope", "stored hierarchy, reference/retarget poses and remaining native fingerprints"},
                   {"changes", changes}};
    if (!unavailable_scopes.empty())
        result["unavailableScopes"] = unavailable_scopes;
    return result;
}
SkeletonComparison compare_skeletons(const Package &before, const Package &after,
                                     const SkeletonOptions &options) {
    auto normalized = options;
    normalized.source_indices = false;
    const auto a = skeleton_snapshot(before, normalized), b = skeleton_snapshot(after, normalized);
    SkeletonComparison result;
    auto verified_tails = [&](const Json &snapshot) {
        for (const auto &body : snapshot.at("skeletons")) {
            options.control.check();
            for (const auto &range : body.at("native").at("opaque"))
                if (range.is_object() && range.value("bytes", std::uint64_t(0)) &&
                    (!range.contains("sha256") || !range.at("sha256").is_string() ||
                     range.at("sha256").get_ref<const std::string &>().size() != 64))
                    return false;
        }
        return true;
    };
    result.complete = a.at("complete").get<bool>() && b.at("complete").get<bool>() && verified_tails(a) &&
                      verified_tails(b);
    auto coverage = source_projection_coverage(before, after, a, b, "skeletons", options.control);
    result.unavailable_scopes = coverage.unavailable;
    result.complete = result.complete && coverage.unavailable.empty();
    walk(a, b, "", result.changes, options.control, &coverage);
    return result;
}
MeshComparison compare_meshes(const MeshGeometry &before, const MeshGeometry &after) {
    MeshComparison result;
    auto a = before.to_json(), b = after.to_json();
    if ((before.render_morphs && !before.render_morphs->ready()) ||
        (after.render_morphs && !after.render_morphs->ready())) {
        result.complete = false;
        result.diagnostics.push_back(
            "Stored render morph identities are unavailable; morph differences are unknown.");
        a.erase("renderMorphs");
        b.erase("renderMorphs");
    } else if (a.contains("renderMorphs") || b.contains("renderMorphs")) {
        const Json empty = {{"schema", "cyuediff.render-morphs/1"},
                            {"ready", true},
                            {"scope", "stored-render-deltas"},
                            {"targets", Json::object()}};
        if (!a.contains("renderMorphs"))
            a["renderMorphs"] = empty;
        if (!b.contains("renderMorphs"))
            b["renderMorphs"] = empty;
    }
    if (a.contains("cloth") || b.contains("cloth")) {
        const Json empty = {{"schema", "cyuediff.cloth/1"},
                            {"scope", "stored-cloth-deformer-mapping"},
                            {"sections", Json::object()}};
        if (!a.contains("cloth"))
            a["cloth"] = empty;
        if (!b.contains("cloth"))
            b["cloth"] = empty;
    }
    if (a.contains("vertexOverlaps") || b.contains("vertexOverlaps")) {
        bool unavailable = false;
        if (!a.contains("vertexOverlaps") || !b.contains("vertexOverlaps")) {
            unavailable = true;
            a.erase("vertexOverlaps");
            b.erase("vertexOverlaps");
        } else {
            auto &x = a["vertexOverlaps"]["sections"], &y = b["vertexOverlaps"]["sections"];
            std::vector<std::string> unknown;
            for (auto i = x.begin(); i != x.end(); ++i)
                if (!y.contains(i.key()))
                    unknown.push_back(i.key());
            for (auto i = y.begin(); i != y.end(); ++i)
                if (!x.contains(i.key()))
                    unknown.push_back(i.key());
            if (!unknown.empty() || !before.vertex_overlaps->unavailable_sections.empty() ||
                !after.vertex_overlaps->unavailable_sections.empty())
                unavailable = true;
            for (const auto &id : unknown) {
                x.erase(id);
                y.erase(id);
            }
        }
        if (unavailable) {
            result.complete = false;
            result.diagnostics.push_back("Stored vertex overlap tables are unavailable for some sections; "
                                         "those relations are unknown.");
        }
    }
    if (a.contains("vertexAttributes") || b.contains("vertexAttributes")) {
        if (!before.vertex_attributes || !after.vertex_attributes) {
            result.complete = false;
            result.diagnostics.push_back(
                "Stored vertex attribute coverage is unavailable; those values are unknown.");
            a.erase("vertexAttributes");
            b.erase("vertexAttributes");
        } else {
            if (!a.contains("vertexAttributes"))
                a["vertexAttributes"] = before.vertex_attributes->semantic_snapshot();
            if (!b.contains("vertexAttributes"))
                b["vertexAttributes"] = after.vertex_attributes->semantic_snapshot();
        }
    }
    walk(a, b, "", result.changes);
    return result;
}
DiffResult compare(const Package &before, const Package &after, const OperationControl &control) {
    control.checkpoint("Comparing packages");
    DiffResult result;
    result.complete_input = before.complete_input && after.complete_input;
    result.binary_comparison_available = !before.fingerprints.empty() && !after.fingerprints.empty();
    if (result.binary_comparison_available)
        result.binary_changed = before.fingerprints != after.fingerprints;
    auto context = [](const Package &p) {
        Json result = {{"archive",
                        {{"ue4Version", p.summary.ue4_version},
                         {"ue5Version", p.summary.ue5_version},
                         {"licenseeVersion", p.summary.licensee_version},
                         {"savedEngine", p.summary.saved_engine},
                         {"customVersions", p.summary.custom_versions}}},
                       {"decoderContextSha256", p.decoder_context_sha256},
                       {"sourceTypeContext", p.source_type_context}};
        if (!p.decoder_definitions.empty())
            result["decoderDefinitions"] = p.decoder_definitions;
        return result;
    };
    auto ca = context(before), cb = context(after);
    if (ca != cb || !before.decoder_context_sha256.empty() || !after.decoder_context_sha256.empty() ||
        !before.source_type_context.is_null() || !after.source_type_context.is_null() ||
        !before.decoder_definitions.empty() || !after.decoder_definitions.empty())
        result.source_contexts = {{"before", ca}, {"after", cb}, {"changed", ca != cb}};
    result.source_read_limit_failed = before.source_read_limit_failed || after.source_read_limit_failed;
    if (result.source_read_limit_failed) {
        result.diagnostics.push_back(
            "A source read limit/count guard failed; semantic differences are unavailable. Review source "
            "diagnostics and read limits. Binary fingerprints remain available.");
        return result;
    }
    detail::SourceCoverageOptions coverage_options;
    coverage_options.control = control;
    auto coverage = detail::source_coverage(before, after, coverage_options);
    result.unavailable_scopes = coverage.unavailable;
    auto projection = [&](const Json &a, const Json &b, Json &changes, const std::string &domain) {
        auto masked = detail::projection_coverage(coverage, before, after, a, b, domain, control);
        for (const auto &scope : masked.unavailable)
            result.unavailable_scopes.push_back(scope);
        walk(a, b, "", changes, control, &masked);
        return masked;
    };
    // Each projected model is materialized only for its own comparison, then released.
    {
        const auto a = before.texture_snapshot(), b = after.texture_snapshot();
        projection(a, b, result.texture_changes, "textures");
    }
    {
        const auto a = before.mesh_snapshot(), b = after.mesh_snapshot();
        projection(a, b, result.mesh_changes, "meshes");
    }
    {
        const auto a = before.material_snapshot(), b = after.material_snapshot();
        projection(a, b, result.material_changes, "materials");
    }
    // Match the public snapshot's sorted root/field traversal and owned result semantics.
    walk(dependencies(before, control), dependencies(after, control), "/dependencies", result.changes,
         control);
    append_projection(result.material_changes, "/materials", result.changes, control);
    append_projection(result.mesh_changes, "/meshes", result.changes, control);
    compare_objects(before, after, result.changes, coverage, control);
    detail::merge_name_changes(result.changes, coverage.name_changes);
    append_projection(result.texture_changes, "", result.changes, control);
    projection(before.blueprint_snapshot(), after.blueprint_snapshot(), result.graph_changes, "blueprint");
    CollisionOptions collision_options;
    collision_options.control = control;
    collision_options.source_indices = false;
    projection(collision_snapshot(before, collision_options), collision_snapshot(after, collision_options),
               result.collision_changes, "collisions");
    SkeletonOptions skeleton_options;
    skeleton_options.source_indices = false;
    skeleton_options.control = control;
    projection(skeleton_snapshot(before, skeleton_options), skeleton_snapshot(after, skeleton_options),
               result.skeleton_changes, "skeletons");
    if (!result.unavailable_scopes.empty())
        result.diagnostics.push_back(
            "Some source values or dependent projections are unavailable; known stored changes and opaque "
            "byte fingerprints are retained without inferring removals from unread peers.");
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
