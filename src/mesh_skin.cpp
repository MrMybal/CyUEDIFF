#include <cyuediff/mesh_skin.hpp>
#include <cmath>
#include <set>
namespace cyuediff {
void MeshSkin::validate(const OperationControl &control) const {
    std::map<std::uint32_t, const MeshBone *> ids;
    std::set<std::string> names;
    for (const auto &bone : bones) {
        control.check();
        if (bone.name.empty() || bone.name == "None" || !ids.emplace(bone.id, &bone).second ||
            !names.insert(bone.name).second || bone.parent < -1)
            throw Error("mesh-skin-identity", "Invalid or duplicate source bone");
        for (const auto *values : {&bone.translation, &bone.scale})
            for (const auto v : *values)
                if (!std::isfinite(v))
                    throw Error("mesh-skin-number", "Non-finite source bone transform");
        double norm = 0;
        for (const auto v : bone.rotation) {
            if (!std::isfinite(v))
                throw Error("mesh-skin-number", "Non-finite source bone rotation");
            norm += v * v;
        }
        if (!std::isfinite(norm) || norm <= 1e-24)
            throw Error("mesh-skin-rotation", "Source bone rotation has zero or invalid length");
    }
    // Source IDs can be sparse and parents need not precede children. Detect cycles iteratively.
    std::map<std::uint32_t, unsigned> marks;
    for (const auto &bone : bones) {
        control.check();
        if (marks[bone.id] == 2)
            continue;
        std::vector<std::uint32_t> chain;
        auto current = &bone;
        while (current && marks[current->id] != 2) {
            control.check();
            if (marks[current->id] == 1)
                throw Error("mesh-skin-hierarchy", "Source bone hierarchy has a cycle");
            marks[current->id] = 1;
            chain.push_back(current->id);
            if (current->parent == -1)
                current = nullptr;
            else {
                const auto found = ids.find(static_cast<std::uint32_t>(current->parent));
                if (found == ids.end())
                    throw Error("mesh-skin-reference", "Source parent bone is absent");
                current = found->second;
            }
        }
        for (const auto id : chain)
            marks[id] = 2;
    }
    for (const auto &profile : profiles) {
        control.check();
        if (profile.first.empty())
            throw Error("mesh-skin-profile", "Empty source skin weight profile");
        for (const auto &vertex : profile.second) {
            std::set<std::uint32_t> seen;
            for (const auto &influence : vertex.second) {
                control.check();
                if (!ids.count(influence.bone))
                    throw Error("mesh-skin-reference", "Skin weight references an absent bone");
                if (!seen.insert(influence.bone).second)
                    throw Error("mesh-skin-identity", "Duplicate influence bone on a vertex");
            }
        }
    }
}
Json MeshSkin::to_json() const {
    validate();
    std::map<std::uint32_t, const MeshBone *> ids;
    for (const auto &bone : bones)
        ids.emplace(bone.id, &bone);
    Json table = Json::object(), vertices = Json::object();
    for (const auto &bone : bones)
        table[bone.name] = {{"parent", bone.parent == -1 ? Json(nullptr) : Json(ids.at(bone.parent)->name)},
                            {"rotation", bone.rotation},
                            {"translation", bone.translation},
                            {"scale", bone.scale}};
    for (const auto &profile : profiles) {
        if (profile.first.empty())
            throw Error("mesh-skin-profile", "Empty source skin weight profile");
        for (const auto &vertex : profile.second) {
            Json weights = Json::object();
            for (const auto &influence : vertex.second) {
                const auto bone = ids.find(influence.bone);
                if (bone == ids.end())
                    throw Error("mesh-skin-reference", "Skin weight references an absent bone");
                const auto &name = bone->second->name;
                if (weights.contains(name))
                    throw Error("mesh-skin-identity", "Duplicate influence bone on a vertex");
                weights[name] = influence.raw_weight;
            }
            vertices[std::to_string(vertex.first)][profile.first] = std::move(weights);
        }
    }
    Json profile_names = Json::array();
    for (const auto &profile : profiles)
        profile_names.push_back(profile.first);
    return {{"schema", "cyuediff.mesh-skin/1"},
            {"bones", table},
            {"profiles", profile_names},
            {"vertices", vertices}};
}
} // namespace cyuediff
