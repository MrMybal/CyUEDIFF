#include <cyuediff/skeleton_pose.hpp>
#include <algorithm>
#include <cmath>
namespace cyuediff {
namespace {
using Matrix = std::array<double, 12>;
bool local_matrix(const Json &pose, Matrix &m) {
    auto q = pose.at("rotation").get<std::array<double, 4>>();
    const auto t = pose.at("translation").get<std::array<double, 3>>(),
               s = pose.at("scale").get<std::array<double, 3>>();
    const double norm = std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
    if (!std::isfinite(norm) || std::abs(norm - 1) > 1e-4)
        return false;
    for (auto &x : q)
        x /= norm;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    m = {(1 - 2 * (y * y + z * z)) * s[0], 2 * (x * y - z * w) * s[1],
         2 * (x * z + y * w) * s[2],       t[0],
         2 * (x * y + z * w) * s[0],       (1 - 2 * (x * x + z * z)) * s[1],
         2 * (y * z - x * w) * s[2],       t[1],
         2 * (x * z - y * w) * s[0],       2 * (y * z + x * w) * s[1],
         (1 - 2 * (x * x + y * y)) * s[2], t[2]};
    return true;
}
Matrix compose(const Matrix &parent, const Matrix &local) {
    Matrix out{};
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column) {
            for (unsigned k = 0; k < 3; ++k)
                out[row * 4 + column] += parent[row * 4 + k] * local[k * 4 + column];
            if (column == 3)
                out[row * 4 + column] += parent[row * 4 + 3];
            if (!std::isfinite(out[row * 4 + column]))
                throw Error("skeleton-pose-value", "Reference affine transform overflow");
        }
    return out;
}
} // namespace
Json ReferencePose::to_json() const {
    Json values = Json::object();
    for (const auto &joint : joints)
        values[joint.name] = {{"parent", joint.parent.empty() ? Json(nullptr) : Json(joint.parent)},
                              {"position", joint.ready ? Json(joint.position) : Json(nullptr)},
                              {"ready", joint.ready},
                              {"diagnostics", joint.diagnostics}};
    return {{"schema", "cyuediff.reference-pose/1"},
            {"space", "raw-reference-affine"},
            {"object", object_path},
            {"complete", complete},
            {"joints", std::move(values)},
            {"diagnostics", diagnostics}};
}
ReferencePose reference_pose(const Package &package, const std::string &path,
                             const ReferencePoseOptions &options) {
    auto projection_options = options.skeleton;
    projection_options.source_indices = true;
    const auto model = skeleton_snapshot(package, projection_options);
    const auto &skeletons = model.at("skeletons");
    if (path.empty() && skeletons.size() != 1)
        throw Error("skeleton-selection", "Select exactly one Skeleton object path");
    auto it = path.empty() ? skeletons.begin() : skeletons.find(path);
    if (it == skeletons.end())
        throw Error("skeleton-selection", "Skeleton object path not found");
    ReferencePose result;
    result.object_path = it.key();
    result.complete = it->at("complete").get<bool>();
    const auto &bones = it->at("bones");
    if (bones.size() > options.max_joints)
        throw Error("limit", "Reference pose joint budget exceeded");
    if (!result.complete)
        result.diagnostics.push_back("Stored skeleton projection is incomplete");
    std::vector<std::string> order(bones.size());
    for (auto bone = bones.begin(); bone != bones.end(); ++bone)
        order[bone->at("sourceIndex").get<std::size_t>()] = bone.key();
    std::vector<Matrix> matrices(order.size());
    std::vector<unsigned> depths(order.size());
    std::map<std::string, std::size_t> indices;
    for (std::size_t i = 0; i < order.size(); ++i) {
        options.skeleton.control.checkpoint("Composing reference pose", i, order.size());
        const auto &name = order[i];
        const auto &bone = bones.at(name);
        ReferenceJoint joint;
        joint.name = name;
        if (!bone.at("parent").is_null())
            joint.parent = bone.at("parent").get<std::string>();
        Matrix local{};
        if (!joint.parent.empty()) {
            const auto parent = indices.at(joint.parent);
            if (depths[parent] >= options.max_hierarchy_depth)
                throw Error("limit", "Reference pose hierarchy depth exceeded");
            depths[i] = depths[parent] + 1;
            if (!result.joints[parent].ready)
                joint.diagnostics.push_back("Parent affine transform is unavailable");
        }
        const bool valid = local_matrix(bone.at("localPose"), local);
        if (!valid)
            joint.diagnostics.push_back("Stored quaternion is not unit length");
        if (valid && joint.diagnostics.empty()) {
            matrices[i] = joint.parent.empty() ? local : compose(matrices[indices.at(joint.parent)], local);
            for (const auto x : matrices[i])
                if (!std::isfinite(x))
                    throw Error("skeleton-pose-value", "Reference affine transform overflow");
            joint.position = {matrices[i][3], matrices[i][7], matrices[i][11]};
            joint.ready = true;
        } else
            result.complete = false;
        indices.emplace(name, i);
        result.joints.push_back(std::move(joint));
    }
    return result;
}
} // namespace cyuediff
