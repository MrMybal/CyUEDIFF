#include <cyuediff/physical_cloth.hpp>
#include <charconv>
#include <set>
namespace cyuediff {
namespace {
std::string token(const std::string &s) {
    std::string out;
    for (char c : s)
        out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
struct Diff {
    const PhysicalClothComparisonOptions &o;
    std::size_t values = 0, bytes = 0;
    Json changes = Json::array();
    void walk(const Json *a, const Json *b, const std::string &path, unsigned depth = 0) {
        if (++values > o.max_values || depth > 128)
            throw Error("limit", "Physical cloth comparison value/depth budget exceeded");
        if (!(values & 1023))
            o.control.checkpoint("physical-cloth-diff", values);
        if (a && b && a->is_object() && b->is_object()) {
            auto l = a->begin(), r = b->begin();
            while (l != a->end() || r != b->end()) {
                if (r == b->end() || (l != a->end() && l.key() < r.key())) {
                    walk(&*l, nullptr, path + "/" + token(l.key()), depth + 1);
                    ++l;
                } else if (l == a->end() || r.key() < l.key()) {
                    walk(nullptr, &*r, path + "/" + token(r.key()), depth + 1);
                    ++r;
                } else {
                    walk(&*l, &*r, path + "/" + token(l.key()), depth + 1);
                    ++l;
                    ++r;
                }
            }
            return;
        }
        if (a && b && a->is_array() && b->is_array()) {
            for (std::size_t i = 0; i < std::max(a->size(), b->size()); ++i)
                walk(i < a->size() ? &(*a)[i] : nullptr, i < b->size() ? &(*b)[i] : nullptr,
                     path + "/" + std::to_string(i), depth + 1);
            return;
        }
        if ((!a && !b) || (a && b && *a == *b))
            return;
        if (changes.size() >= o.max_changes)
            throw Error("limit", "Physical cloth change budget exceeded");
        Json row = {{"op", !a ? "add" : !b ? "remove" : "replace"}, {"path", path}};
        if (a)
            row["before"] = *a;
        if (b)
            row["after"] = *b;
        const auto n = row.dump().size();
        if (n > o.max_report_bytes - bytes)
            throw Error("limit", "Physical cloth change report exceeds byte budget");
        bytes += n;
        changes.push_back(std::move(row));
    }
};
Json semantic(const PhysicalClothAsset &a, const OperationControl &c) {
    auto j = a.to_json(true, c);
    for (const auto *key : {"schema", "profile", "scope", "object", "available", "provenance", "diagnostics",
                            "defaultsResolved", "simulationEvaluated"})
        j.erase(key);
    for (auto &l : j["lods"]) {
        c.check();
        l.erase("geometryAvailable");
        l.erase("bounds");
        l.erase("triangles");
        for (const auto *name : {"transitionUp", "transitionDown"}) {
            for (const auto *key : {"sourceLOD", "records", "sourceBoundsVerified", "wrapAvailable"})
                l[name].erase(key);
            for (auto &r : l[name]["values"]) {
                c.check();
                r.erase("padding");
            }
        }
    }
    Json configs = Json::object();
    for (const auto &cfg : j["configs"]) {
        c.check();
        configs[cfg["key"].get<std::string>()] = cfg;
        configs[cfg["key"].get<std::string>()].erase("key");
        configs[cfg["key"].get<std::string>()].erase("unavailableFields");
    }
    j["configs"] = std::move(configs);
    return j;
}
} // namespace
Json compare_physical_cloth(const PhysicalClothAsset &a, const PhysicalClothAsset &b,
                            const PhysicalClothComparisonOptions &o) {
    if (!o.max_values || o.max_values > 128'000'000 || !o.max_changes || o.max_changes > 1'000'000 ||
        !o.max_report_bytes || o.max_report_bytes > 1024ull * 1024 * 1024)
        throw Error("limit", "Invalid physical comparison limits");
    o.control.check();
    Json out = {{"schema", "cyuediff.physical-cloth-diff/1"},
                {"scope", "selected-stored-physical-cloth"},
                {"beforeObject", a.object_path()},
                {"afterObject", b.object_path()},
                {"explicitObjectPair", o.explicit_object_pair},
                {"complete", false},
                {"status", "not-comparable"},
                {"changes", Json::array()},
                {"unavailableFields", Json::array()}};
    if (!a.available() || !b.available()) {
        out["diagnostic"] = "An unavailable peer is not an empty physical cloth asset.";
        return out;
    }
    if (a.object_path() != b.object_path() && !o.explicit_object_pair) {
        out["diagnostic"] = "Select an explicit physical cloth object pair.";
        return out;
    }
    auto x = semantic(a, o.control), y = semantic(b, o.control);
    bool complete = true;
    auto unknown = [&](const std::string &path) {
        complete = false;
        out["unavailableFields"].push_back(path);
    };
    auto common = [&](Json &left, Json &right, const std::string &path) {
        std::set<std::string> keys;
        for (auto it = left.begin(); it != left.end(); ++it)
            keys.insert(it.key());
        for (auto it = right.begin(); it != right.end(); ++it)
            keys.insert(it.key());
        for (const auto &key : keys)
            if (!left.contains(key) || !right.contains(key)) {
                unknown(path + "/" + token(key));
                left.erase(key);
                right.erase(key);
            }
    };
    if (x["referenceBoneIndex"].is_null() || y["referenceBoneIndex"].is_null()) {
        unknown("/referenceBoneIndex");
        x.erase("referenceBoneIndex");
        y.erase("referenceBoneIndex");
    }
    for (std::size_t i = 0; i < std::min(x["lods"].size(), y["lods"].size()); ++i) {
        auto &left = x["lods"][i]["parameters"], &right = y["lods"][i]["parameters"];
        for (const auto *key : {"MaxBoneWeights", "NumFixedVerts", "SkinningKernelRadius",
                                "bUseMultipleInfluences", "bSmoothTransition"})
            if (!left.contains(key) || !right.contains(key)) {
                unknown("/lods/" + std::to_string(i) + "/parameters/" + key);
                left.erase(key);
                right.erase(key);
            }
        common(left, right, "/lods/" + std::to_string(i) + "/parameters");
    }
    for (std::size_t i = 0; i < std::min(x["lods"].size(), y["lods"].size()); ++i) {
        auto &left = x["lods"][i], &right = y["lods"][i];
        if (!left["weightMapReplaced"].get<bool>() || !right["weightMapReplaced"].get<bool>()) {
            unknown("/lods/" + std::to_string(i) + "/weightMaps/@unresolved-defaults");
            common(left["weightMaps"], right["weightMaps"], "/lods/" + std::to_string(i) + "/weightMaps");
        }
    }
    if (!x["configMapReplaced"].get<bool>() || !y["configMapReplaced"].get<bool>()) {
        unknown("/configs/@unresolved-defaults");
        common(x["configs"], y["configs"], "/configs");
    }
    for (auto *root : {&x, &y}) {
        root->erase("configMapReplaced");
        root->erase("removedConfigEntries");
        for (auto &lod : (*root)["lods"]) {
            lod.erase("weightMapReplaced");
            lod.erase("removedWeightMapEntries");
        }
    }
    for (const auto &cfg : a.configs())
        if (!cfg.available || !cfg.unavailable_fields.empty())
            unknown("/configs/" + token(cfg.key));
    for (const auto &cfg : b.configs())
        if (!cfg.available || !cfg.unavailable_fields.empty())
            unknown("/configs/" + token(cfg.key));
    for (auto it = x["configs"].begin(); it != x["configs"].end(); ++it)
        if (y["configs"].contains(it.key())) {
            auto &left = it.value(), &right = y["configs"][it.key()];
            if (!left["available"].get<bool>() || !right["available"].get<bool>() ||
                left["class"] != right["class"]) {
                left.erase("values");
                right.erase("values");
            } else
                common(left["values"], right["values"], "/configs/" + token(it.key()) + "/values");
        }
    for (auto it = x["configs"].begin(); it != x["configs"].end(); ++it) {
        if (y["configs"].contains(it.key())) {
            auto &right = y["configs"][it.key()];
            if (it.value()["class"].get<std::string>().empty() || right["class"].get<std::string>().empty()) {
                it.value().erase("class");
                right.erase("class");
            }
        }
        it.value().erase("available");
    }
    for (auto &config : y["configs"])
        config.erase("available");
    Diff d{o};
    d.walk(&x, &y, "");
    o.control.check();
    out["changes"] = std::move(d.changes);
    out["complete"] = complete;
    out["status"] = complete ? (out["changes"].empty() ? "identical" : "changed") : "incomplete";
    const auto ac = a.to_json(false, o.control)["provenance"], bc = b.to_json(false, o.control)["provenance"];
    out["beforeContext"] = ac;
    out["afterContext"] = bc;
    out["contextChanged"] = ac["schemaContextSha256"] != bc["schemaContextSha256"] ||
                            ac["scriptObjectsSha256"] != bc["scriptObjectsSha256"];
    out["rawPackageChanged"] = ac["packageSha256"] != bc["packageSha256"];
    return out;
}
Json verify_cloth_source_bounds(const ClothSection &s, std::uint32_t bias, const PhysicalClothAsset &a,
                                std::uint32_t lod, bool explicit_bias, const OperationControl &c) {
    c.check();
    if (!a.available() || lod >= a.lods().size() || bias >= s.lod_biases.size())
        throw Error("physical-cloth-selection", "Select stored cloth mappings and an available physical LOD");
    std::array<std::uint32_t, 4> guid{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto begin = a.guid().data() + i * 8;
        const auto parsed = std::from_chars(begin, begin + 8, guid[i], 16);
        if (parsed.ec != std::errc{})
            throw Error("physical-cloth-value", "Invalid physical GUID");
    }
    const bool identity = bias == 0 && guid != std::array<std::uint32_t, 4>{} && guid == s.asset_guid &&
                          s.asset_lod >= 0 && std::uint32_t(s.asset_lod) == lod;
    if (bias == 0 && !identity)
        throw Error("physical-cloth-binding",
                    "Base mapping GUID/LOD does not identify the selected physical asset");
    if (bias && !explicit_bias)
        throw Error(
            "physical-cloth-binding",
            "A biased mapping needs an explicit physical source; asset_lod+bias is not a stored binding");
    const auto end = std::uint64_t(s.first_vertex) + s.vertices;
    if (end > UINT32_MAX)
        throw Error("physical-cloth-value", "Render section vertex range overflows");
    s.validate(static_cast<std::uint32_t>(end), 5, c);
    std::size_t active = 0, inactive = 0;
    const auto n = a.lods()[lod].vertices.size();
    for (const auto &m : s.lod_biases[bias]) {
        c.check();
        if (!m.simulated()) {
            ++inactive;
            continue;
        }
        ++active;
        for (unsigned i = 0; i < 3; ++i)
            if (m.source_indices[i] >= n)
                throw Error("physical-cloth-binding",
                            "Render cloth mapping exceeds selected physical vertices");
    }
    return {{"schema", "cyuediff.cloth-source-bounds/1"},
            {"physicalObject", a.object_path()},
            {"physicalLOD", lod},
            {"section", s.section},
            {"lodBias", bias},
            {"sourceIdentityVerified", identity},
            {"explicitBiasedSource", bool(bias && explicit_bias)},
            {"sourceBoundsVerified", !s.lod_biases[bias].empty()},
            {"physicalVertices", n},
            {"activeRecords", active},
            {"inactiveRecords", inactive}};
}
} // namespace cyuediff
