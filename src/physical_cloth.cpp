#include <cyuediff/physical_cloth.hpp>
#include <cyuediff/zen.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <set>
namespace cyuediff {
namespace {
constexpr auto Root = "/Script/ClothingSystemRuntimeCommon.ClothingAssetCommon";
constexpr auto Lod = "/Script/ClothingSystemRuntimeCommon.ClothLODDataCommon";
constexpr auto Phys = "/Script/ClothingSystemRuntimeCommon.ClothPhysicalMeshData";
constexpr auto Bone = "/Script/ClothingSystemRuntimeInterface.ClothVertBoneData";
constexpr auto Point = "/Script/ClothingSystemRuntimeCommon.PointWeightMap";
void need(bool ok, const std::string &message) {
    if (!ok)
        throw Error("physical-cloth-value", message);
}
struct Reader {
    const PhysicalClothOptions &o;
    std::size_t values = 0, bytes = 0, texts = 0, vertices = 0, indices = 0, tethers = 0, transitions = 0;
    void count(std::size_t &used, std::size_t n, std::size_t max, const char *message) {
        if (n > max - used)
            throw Error("limit", message);
        used += n;
    }
    void tick() {
        count(values, 1, o.max_values, "Physical cloth value budget exceeded");
        if (!(values & 1023))
            o.control.checkpoint("physical-cloth", values);
    }
    void reserve(std::size_t n) {
        count(bytes, n, o.max_model_bytes, "Physical cloth model budget exceeded");
    }
    void text(const std::string &s) {
        count(texts, s.size(), o.max_text_bytes, "Physical cloth text budget exceeded");
    }
    const Json *property(const Json &p, const std::string &schema, const std::string &name, const char *type,
                         bool required = true) {
        tick();
        need(p.is_object(), "Expected qualified physical cloth fields");
        auto i = p.find(schema + "." + name);
        if (i == p.end()) {
            if (required)
                throw Error("physical-cloth-missing", "Missing stored field " + schema + "." + name);
            return nullptr;
        }
        need(i->is_object() && i->contains("value") && i->value("type", Json{}) == type,
             "Unexpected type for " + schema + "." + name);
        return &i->at("value");
    }
    std::int64_t integer(const Json &v, std::int64_t low, std::int64_t high) {
        tick();
        need(v.is_number_integer() &&
                 (!v.is_number_unsigned() || v.get<std::uint64_t>() <= std::uint64_t(INT64_MAX)),
             "Expected bounded physical integer");
        const auto n = v.get<std::int64_t>();
        need(n >= low && n <= high, "Physical integer outside its declared range");
        return n;
    }
    float scalar(const Json &v) {
        tick();
        need(v.is_number(), "Expected stored physical float32");
        const auto d = v.get<double>();
        const auto f = static_cast<float>(d);
        need(std::isfinite(d) && std::isfinite(f) && double(f) == d, "Expected exact finite stored float32");
        return f;
    }
    std::string string(const Json &v) {
        tick();
        need(v.is_string(), "Expected physical string");
        const auto &s = v.get_ref<const std::string &>();
        text(s);
        return s;
    }
    const Json &array(const Json &v) {
        tick();
        need(v.is_array(), "Expected physical array");
        return v;
    }
    std::vector<float> floats(const Json &v) {
        const auto &a = array(v);
        reserve(a.size() * sizeof(float));
        std::vector<float> out;
        out.reserve(a.size());
        for (const auto &x : a)
            out.push_back(scalar(x));
        return out;
    }
    std::vector<std::array<float, 3>> vectors(const Json &v) {
        const auto &a = array(v);
        reserve(a.size() * sizeof(std::array<float, 3>));
        std::vector<std::array<float, 3>> out;
        out.reserve(a.size());
        for (const auto &x : a) {
            need(array(x).size() == 3, "Physical vector width differs");
            out.push_back({scalar(x[0]), scalar(x[1]), scalar(x[2])});
        }
        return out;
    }
    std::vector<std::uint32_t> ids(const Json &v, std::uint32_t max) {
        const auto &a = array(v);
        reserve(a.size() * 4);
        std::vector<std::uint32_t> out;
        out.reserve(a.size());
        for (const auto &x : a)
            out.push_back(static_cast<std::uint32_t>(integer(x, 0, max)));
        return out;
    }
    const Json &map(const Json &v) {
        tick();
        need(v.is_object() && v.value("kind", Json{}) == "stored-map-delta" && v.contains("replace") &&
                 v.at("replace").is_boolean() && v.contains("removed") && v.at("removed").is_object() &&
                 v.contains("entries") && v.at("entries").is_object(),
             "Expected stored physical map delta");
        need(!v.at("replace").get<bool>() || v.at("removed").empty(),
             "A replacement physical map cannot also contain removals");
        return v.at("entries");
    }
    std::vector<std::vector<PhysicalClothTether>> tether(const Json &v, std::size_t n) {
        need(v.is_object() && v.value("properties", Json{}) == Json::object() && v.contains("native") &&
                 v.at("native").is_object() && v.at("native").contains("batches"),
             "Expected certified tether native tail");
        const auto &a = array(v.at("native").at("batches"));
        reserve(a.size() * sizeof(std::vector<PhysicalClothTether>));
        std::vector<std::vector<PhysicalClothTether>> out;
        out.reserve(a.size());
        for (const auto &b : a) {
            array(b);
            count(tethers, b.size(), o.max_tethers, "Physical tether aggregate limit exceeded");
            reserve(b.size() * sizeof(PhysicalClothTether));
            reserve(b.size() * 64);
            std::vector<PhysicalClothTether> batch;
            batch.reserve(b.size());
            std::set<std::int32_t> particles;
            for (const auto &row : b) {
                need(row.is_object() && row.size() == 3 && row.contains("anchor") &&
                         row.contains("particle") && row.contains("length"),
                     "Malformed physical tether tuple");
                const auto anchor = integer(row["anchor"], 0, INT32_MAX),
                           particle = integer(row["particle"], 0, INT32_MAX);
                const auto length = scalar(row["length"]);
                need(std::size_t(anchor) < n && std::size_t(particle) < n && length >= 0,
                     "Tether endpoint/length exceeds physical mesh");
                need(particles.insert(static_cast<std::int32_t>(particle)).second,
                     "Tether batch repeats a writable particle");
                batch.push_back(
                    {static_cast<std::int32_t>(anchor), static_cast<std::int32_t>(particle), length});
            }
            out.push_back(std::move(batch));
        }
        return out;
    }
    std::vector<ClothMapping> mappings(const Json &v) {
        const auto &a = array(v);
        count(transitions, a.size(), o.max_transitions, "Physical transition aggregate limit exceeded");
        reserve(a.size() * sizeof(ClothMapping));
        std::vector<ClothMapping> out;
        out.reserve(a.size());
        for (const auto &row : a) {
            const auto hex = string(row);
            need(hex.size() == 128, "Expected 64-byte physical transition record");
            std::array<std::uint8_t, 64> record_bytes{};
            auto nibble = [](char c) {
                return c >= '0' && c <= '9'   ? c - '0'
                       : c >= 'a' && c <= 'f' ? c - 'a' + 10
                       : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                              : -1;
            };
            for (std::size_t i = 0; i < 64; ++i) {
                const int high = nibble(hex[i * 2]), low = nibble(hex[i * 2 + 1]);
                need(high >= 0 && low >= 0, "Invalid physical transition hex byte");
                record_bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
            }
            std::size_t offset = 0;
            auto u32 = [&] {
                std::uint32_t n = 0;
                for (unsigned b = 0; b < 4; ++b)
                    n |= std::uint32_t(record_bytes[offset++]) << (b * 8);
                return n;
            };
            auto f32 = [&] {
                const auto bits = u32();
                float f;
                std::memcpy(&f, &bits, 4);
                return f;
            };
            ClothMapping m;
            for (auto *vector : {&m.position, &m.normal, &m.tangent})
                for (auto &f : *vector)
                    f = f32();
            for (auto &i : m.source_indices) {
                i = std::uint16_t(record_bytes[offset]) | std::uint16_t(record_bytes[offset + 1]) << 8;
                offset += 2;
            }
            m.weight = f32();
            m.padding = u32();
            m.validate();
            out.push_back(m);
        }
        return out;
    }
};
Json bounds(const std::vector<std::array<float, 3>> &v, const OperationControl &control) {
    if (v.empty())
        return nullptr;
    auto low = v[0], high = v[0];
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!(i & 1023))
            control.check();
        for (unsigned k = 0; k < 3; ++k) {
            low[k] = std::min(low[k], v[i][k]);
            high[k] = std::max(high[k], v[i][k]);
        }
    }
    return {{"min", low}, {"max", high}};
}
const Json *decoded(const ZenExport &e) {
    if (!e.property_read.is_object() || e.property_read.value("status", Json{}) != "decoded")
        return nullptr;
    const auto &r = e.property_read;
    need(r.contains("offset") && r["offset"].is_number_unsigned() && r.contains("consumedBytes") &&
             r["consumedBytes"].is_number_unsigned(),
         "Missing physical property boundary");
    const auto start = r["offset"].get<std::uint64_t>(), n = r["consumedBytes"].get<std::uint64_t>();
    need(start <= e.size && n <= e.size - start, "Physical property boundary exceeds export");
    need(!r.contains("defaultsResolved") ||
             (r["defaultsResolved"].is_boolean() && !r["defaultsResolved"].get<bool>()),
         "Physical projection does not support resolved defaults");
    need(r.contains("properties") && r["properties"].is_object(), "Expected physical property snapshot");
    return &r["properties"];
}
Json bone_json(const PhysicalClothBone &b) {
    return {{"influences", b.influences}, {"indices", b.indices}, {"weights", b.weights}};
}
struct Report {
    std::size_t left;
    const OperationControl &control;
    void use(std::size_t n) {
        control.check();
        if (n > left)
            throw Error("limit", "Physical cloth report value budget exceeded");
        left -= n;
    }
    template <class T> Json array(const std::vector<T> &v, std::size_t cost = 1) {
        use(v.size() * cost + 1);
        return Json(v);
    }
};
Json transition_json(const PhysicalClothTransition &t, bool include, Report &r) {
    Json out = {{"sourceLOD", t.source_lod ? Json(*t.source_lod) : Json(nullptr)},
                {"records", t.records.size()},
                {"sourceBoundsVerified", t.source_bounds_verified},
                {"wrapAvailable", t.wrap_available}};
    if (include) {
        out["values"] = Json::array();
        for (const auto &m : t.records) {
            r.use(32);
            out["values"].push_back(m.to_json(true));
        }
    }
    return out;
}
const PhysicalClothLod &select(const PhysicalClothAsset &a, std::uint32_t lod) {
    if (!a.available() || lod >= a.lods().size())
        throw Error("physical-cloth-selection", "Select an available physical cloth LOD");
    return a.lods()[lod];
}
} // namespace
PhysicalClothAsset read_physical_cloth(const ZenPackage &p, const PhysicalClothOptions &o,
                                       std::size_t export_index) {
    if (o.profile != PhysicalClothProfile::unreal_5_3_windows || p.profile != "ue5.3")
        throw Error("physical-cloth-profile", "Select the explicit UE 5.3 Windows physical cloth profile");
    if (!o.max_lods || o.max_lods > 128 || !o.max_vertices || o.max_vertices > 16'000'000 || !o.max_indices ||
        o.max_indices > 64'000'000 || !o.max_tethers || o.max_tethers > 16'000'000 || !o.max_transitions ||
        o.max_transitions > 16'000'000 || !o.max_values || o.max_values > 128'000'000 || !o.max_model_bytes ||
        o.max_model_bytes > 2ull * 1024 * 1024 * 1024 || !o.max_text_bytes ||
        o.max_text_bytes > 256 * 1024 * 1024 || !o.max_report_values || o.max_report_values > 128'000'000)
        throw Error("limit", "Invalid physical cloth limits");
    o.control.checkpoint("physical-cloth");
    auto relevant = [](const ZenExport &e) { return e.class_name == Root && !(e.flags & 0x30); };
    if (export_index == SIZE_MAX) {
        for (std::size_t i = 0; i < p.exports.size(); ++i) {
            if (!(i & 1023))
                o.control.check();
            if (relevant(p.exports[i])) {
                if (export_index != SIZE_MAX)
                    throw Error("physical-cloth-selection", "Select one physical cloth export");
                export_index = i;
            }
        }
    }
    if (export_index >= p.exports.size() || !relevant(p.exports[export_index]))
        throw Error("physical-cloth-selection", "Physical cloth export is missing or not an instance");
    const auto &e = p.exports[export_index];
    PhysicalClothAsset out;
    out.object_ = e.path;
    out.package_sha_ = p.sha256;
    out.context_sha_ = p.schema_context_sha256;
    out.scripts_sha_ = p.script_objects_sha256;
    out.report_values_ = o.max_report_values;
    Reader r{o};
    for (const auto *s : {&out.object_, &out.package_sha_, &out.context_sha_, &out.scripts_sha_})
        r.text(*s);
    const auto *props = decoded(e);
    if (!props) {
        out.diagnostics_.push_back("Physical cloth properties are unavailable; no empty asset is inferred.");
        return out;
    }
    try {
        out.guid_ = r.string(*r.property(*props, "/Script/ClothingSystemRuntimeInterface.ClothingAssetBase",
                                         "AssetGuid", "StructProperty"));
        need(out.guid_.size() == 32 && std::all_of(out.guid_.begin(), out.guid_.end(),
                                                   [](char c) {
                                                       return (c >= '0' && c <= '9') ||
                                                              (c >= 'a' && c <= 'f') ||
                                                              (c >= 'A' && c <= 'F');
                                                   }),
             "Invalid stored physical cloth GUID");
        for (auto &c : out.guid_)
            if (c >= 'A' && c <= 'F')
                c = char(c - 'A' + 'a');
        r.reserve(r.array(*r.property(*props, Root, "LodMap", "ArrayProperty")).size() *
                  sizeof(std::int32_t));
        for (const auto &x : r.array(*r.property(*props, Root, "LodMap", "ArrayProperty")))
            out.lod_map_.push_back(static_cast<std::int32_t>(r.integer(x, -1, INT32_MAX)));
        r.reserve(r.array(*r.property(*props, Root, "UsedBoneIndices", "ArrayProperty")).size() *
                  sizeof(std::int32_t));
        for (const auto &x : r.array(*r.property(*props, Root, "UsedBoneIndices", "ArrayProperty")))
            out.bone_indices_.push_back(static_cast<std::int32_t>(r.integer(x, 0, INT32_MAX)));
        r.reserve(r.array(*r.property(*props, Root, "UsedBoneNames", "ArrayProperty")).size() *
                  sizeof(std::string));
        for (const auto &x : r.array(*r.property(*props, Root, "UsedBoneNames", "ArrayProperty")))
            out.bone_names_.push_back(r.string(x));
        need(out.bone_indices_.size() == out.bone_names_.size(), "Physical used bone identities disagree");
        if (const auto v = r.property(*props, Root, "ReferenceBoneIndex", "IntProperty", false))
            out.reference_bone_ = static_cast<std::int32_t>(r.integer(*v, -1, INT32_MAX));
        const auto &lods = r.array(*r.property(*props, Root, "LodData", "ArrayProperty"));
        if (lods.size() > o.max_lods)
            throw Error("limit", "Physical LOD count exceeds limit");
        r.reserve(lods.size() * sizeof(PhysicalClothLod));
        for (const auto &v : lods) {
            o.control.check();
            need(v.is_object() && v.contains("properties") && v.contains("native"),
                 "Expected hybrid physical LOD");
            const auto &lp = v.at("properties"), &native = v.at("native");
            need(native.is_object() && native.contains("transitionUp") && native.contains("transitionDown"),
                 "Missing physical transition tails");
            const auto &pp = *r.property(lp, Lod, "PhysicalMeshData", "StructProperty");
            PhysicalClothLod l;
            const auto &verts = r.array(*r.property(pp, Phys, "Vertices", "ArrayProperty"));
            r.count(r.vertices, verts.size(), o.max_vertices, "Physical vertex aggregate limit exceeded");
            l.vertices = r.vectors(verts);
            const auto n = l.vertices.size();
            l.normals = r.vectors(*r.property(pp, Phys, "Normals", "ArrayProperty"));
            need(l.normals.empty() || l.normals.size() == n, "Physical normal count differs");
            const auto &indices = r.array(*r.property(pp, Phys, "Indices", "ArrayProperty"));
            r.count(r.indices, indices.size(), o.max_indices, "Physical index aggregate limit exceeded");
            need(indices.size() % 3 == 0, "Physical triangle index count differs");
            l.indices = r.ids(indices, UINT32_MAX);
            for (auto id : l.indices)
                need(id < n, "Physical triangle index exceeds vertices");
            l.inverse_masses = r.floats(*r.property(pp, Phys, "InverseMasses", "ArrayProperty"));
            need(l.inverse_masses.empty() || l.inverse_masses.size() == n,
                 "Physical inverse mass count differs");
            for (auto f : l.inverse_masses)
                need(f >= 0, "Negative physical inverse mass");
            l.self_collision_indices =
                r.ids(*r.property(pp, Phys, "SelfCollisionIndices", "ArrayProperty"), UINT32_MAX);
            for (auto id : l.self_collision_indices)
                need(id < n, "Physical self-collision index exceeds vertices");
            const auto &bones = r.array(*r.property(pp, Phys, "BoneData", "ArrayProperty"));
            need(bones.empty() || bones.size() == n, "Physical bone data count differs");
            r.reserve(bones.size() * sizeof(PhysicalClothBone));
            l.bones.reserve(bones.size());
            for (const auto &b : bones) {
                PhysicalClothBone row;
                row.influences = static_cast<std::int32_t>(
                    r.integer(*r.property(b, Bone, "NumInfluences", "IntProperty"), 0, 12));
                for (unsigned k = 0; k < 12; ++k) {
                    row.indices[k] = static_cast<std::uint16_t>(r.integer(
                        *r.property(b, Bone, "BoneIndices[" + std::to_string(k) + "]", "UInt16Property"), 0,
                        65535));
                    row.weights[k] = r.scalar(
                        *r.property(b, Bone, "BoneWeights[" + std::to_string(k) + "]", "FloatProperty"));
                    if (k < static_cast<unsigned>(row.influences))
                        need(row.indices[k] < out.bone_indices_.size() && row.weights[k] >= 0 &&
                                 row.weights[k] <= 1,
                             "Physical active bone binding/weight differs");
                }
                l.bones.push_back(row);
            }
            const auto &weight_map_value = *r.property(pp, Phys, "WeightMaps", "MapProperty");
            const auto &maps = r.map(weight_map_value);
            l.weight_maps_replaced = weight_map_value["replace"].get<bool>();
            r.reserve(weight_map_value["removed"].size() * sizeof(std::uint32_t));
            for (auto it = weight_map_value["removed"].begin(); it != weight_map_value["removed"].end();
                 ++it) {
                const auto id = static_cast<std::uint32_t>(r.integer(it.value(), 0, UINT32_MAX));
                need(it.key() == std::to_string(id), "Noncanonical removed physical weight-map identity");
                l.removed_weight_maps.push_back(id);
            }
            for (auto it = maps.begin(); it != maps.end(); ++it) {
                need(it->is_object() && it->contains("key") && it->contains("value"),
                     "Malformed physical weight-map entry");
                const auto target = static_cast<std::uint32_t>(r.integer(it->at("key"), 0, UINT32_MAX));
                need(it.key() == std::to_string(target), "Physical weight-map key is not canonical");
                auto values = r.floats(*r.property(it->at("value"), Point, "Values", "ArrayProperty"));
                need(values.empty() || values.size() == n, "Physical weight map count differs");
                r.reserve(sizeof(std::pair<const std::uint32_t, std::vector<float>>) + 64);
                l.weight_maps.emplace(target, std::move(values));
            }
            l.euclidean_tethers = r.tether(*r.property(pp, Phys, "EuclideanTethers", "StructProperty"), n);
            l.geodesic_tethers = r.tether(*r.property(pp, Phys, "GeodesicTethers", "StructProperty"), n);
            for (const auto *name : {"MaxBoneWeights", "NumFixedVerts"})
                if (const auto value = r.property(pp, Phys, name, "IntProperty", false))
                    l.parameters[name] = r.integer(
                        *value, 0, std::string(name) == "MaxBoneWeights" ? 12 : static_cast<std::int64_t>(n));
            if (const auto value = r.property(lp, Lod, "SkinningKernelRadius", "FloatProperty", false))
                l.parameters["SkinningKernelRadius"] = r.scalar(*value);
            for (const auto *name : {"bUseMultipleInfluences", "bSmoothTransition"})
                if (const auto value = r.property(lp, Lod, name, "BoolProperty", false)) {
                    r.tick();
                    need(value->is_boolean(), "Expected physical boolean");
                    l.parameters[name] = *value;
                }
            l.up.records = r.mappings(native["transitionUp"]);
            l.down.records = r.mappings(native["transitionDown"]);
            l.bounds = bounds(l.vertices, o.control);
            if (o.geometry && l.normals.size() == n) {
                r.reserve(n * (sizeof(MeshVertex) + sizeof(MeshInstance)) +
                          l.indices.size() / 3 * sizeof(MeshTriangle));
                auto g = std::make_shared<MeshGeometry>();
                g->vertices.reserve(n);
                g->instances.reserve(n);
                g->triangles.reserve(l.indices.size() / 3);
                for (std::size_t i = 0; i < n; ++i) {
                    if (!(i & 1023))
                        o.control.check();
                    g->vertices.push_back({static_cast<std::uint32_t>(i), l.vertices[i]});
                    MeshInstance instance;
                    instance.id = instance.vertex = static_cast<std::uint32_t>(i);
                    instance.normal = l.normals[i];
                    g->instances.push_back(std::move(instance));
                }
                for (std::size_t i = 0; i < l.indices.size(); i += 3) {
                    if (!(i & 1023))
                        o.control.check();
                    g->triangles.push_back({static_cast<std::uint32_t>(i / 3),
                                            static_cast<std::uint32_t>(i / 3),
                                            0,
                                            {l.indices[i], l.indices[i + 1], l.indices[i + 2]}});
                }
                g->bounds = l.bounds;
                g->material_slots[0] = "Physical cloth";
                g->attributes = {{"scope", "stored-physical-cloth"},
                                 {"tangentsAvailable", false},
                                 {"uvsAvailable", false},
                                 {"colorsAvailable", false}};
                l.geometry = std::move(g);
            }
            out.lods_.push_back(std::move(l));
        }
        for (auto map : out.lod_map_)
            need(map == -1 || std::size_t(map) < out.lods_.size(), "Stored physical LOD map exceeds LODs");
        for (std::size_t i = 0; i < out.lods_.size(); ++i) {
            auto &l = out.lods_[i];
            for (bool up : {true, false}) {
                auto &t = up ? l.up : l.down;
                need(t.records.empty() || t.records.size() == l.vertices.size(),
                     "Physical transition does not contain one record per target vertex");
                if ((up && i == 0) || (!up && i + 1 == out.lods_.size())) {
                    if (!t.records.empty())
                        out.diagnostics_.push_back("Boundary transition retained without a source LOD.");
                    continue;
                }
                t.source_lod = static_cast<std::uint32_t>(up ? i - 1 : i + 1);
                const auto &source = out.lods_[*t.source_lod];
                t.source_bounds_verified = true;
                t.wrap_available =
                    t.records.size() == l.vertices.size() && source.normals.size() == source.vertices.size();
                for (const auto &m : t.records) {
                    r.tick();
                    for (unsigned k = 0; k < 3; ++k)
                        need(m.source_indices[k] < source.vertices.size(),
                             "Physical transition source index exceeds adjacent LOD");
                    for (float f : m.position)
                        t.wrap_available = t.wrap_available && std::isfinite(f);
                }
            }
        }
        std::map<std::string, const ZenExport *> exports;
        for (const auto &row : p.exports) {
            r.tick();
            need(exports.emplace(row.path, &row).second,
                 "Duplicate cooked object identity in physical projection");
        }
        const auto &config_map = *r.property(*props, Root, "ClothConfigs", "MapProperty");
        const auto &configs = r.map(config_map);
        out.configs_replaced_ = config_map["replace"].get<bool>();
        r.reserve(config_map["removed"].size() * sizeof(std::string));
        for (auto it = config_map["removed"].begin(); it != config_map["removed"].end(); ++it) {
            const auto key = r.string(it.value());
            need(it.key() == Json(key).dump(), "Noncanonical removed physical configuration identity");
            out.removed_configs_.push_back(key);
        }
        r.reserve(configs.size() * (sizeof(PhysicalClothConfig) + 512));
        for (auto it = configs.begin(); it != configs.end(); ++it) {
            need(it->is_object() && it->contains("key") && it->contains("value"),
                 "Malformed physical config link");
            PhysicalClothConfig config;
            config.key = r.string(it->at("key"));
            need(it.key() == Json(config.key).dump(), "Physical config key is not canonical");
            const auto &ref = it->at("value");
            need(ref.is_object() && ref.contains("kind") && ref["kind"].is_string(),
                 "Malformed physical config reference");
            const auto kind = r.string(ref["kind"]);
            if (ref.contains("path"))
                config.object_path = r.string(ref["path"]);
            const auto target = exports.find(config.object_path);
            if (kind == "export" && target != exports.end()) {
                config.class_name = target->second->class_name;
                r.text(config.class_name);
                const auto cp = decoded(*target->second);
                const bool chaos = config.class_name == "/Script/ChaosCloth.ChaosClothConfig",
                           shared = config.class_name == "/Script/ChaosCloth.ChaosClothSharedSimConfig";
                config.available = cp && (chaos || shared);
                if (config.available) {
                    for (const auto *name :
                         chaos ? std::vector<const char *>{"DampingCoefficient", "GravityScale"}
                               : std::vector<const char *>{}) {
                        if (const auto v = r.property(*cp, config.class_name, name, "FloatProperty", false))
                            config.values[name] = r.scalar(*v);
                        else
                            config.unavailable_fields.push_back(name);
                    }
                    if (chaos) {
                        const auto weighted = r.property(*cp, config.class_name, "EdgeStiffnessWeighted",
                                                         "StructProperty", false);
                        for (const auto *name : {"Low", "High"}) {
                            const auto value =
                                weighted ? r.property(*weighted, "/Script/ChaosCloth.ChaosClothWeightedValue",
                                                      name, "FloatProperty", false)
                                         : nullptr;
                            if (value)
                                config.values[std::string("EdgeStiffnessWeighted.") + name] =
                                    r.scalar(*value);
                            else
                                config.unavailable_fields.push_back(std::string("EdgeStiffnessWeighted.") +
                                                                    name);
                        }
                    }
                    if (shared) {
                        if (const auto v =
                                r.property(*cp, config.class_name, "IterationCount", "IntProperty", false))
                            config.values["IterationCount"] = r.integer(*v, 0, INT32_MAX);
                        else
                            config.unavailable_fields.push_back("IterationCount");
                    }
                }
            }
            out.configs_.push_back(std::move(config));
        }
        out.available_ = true;
    } catch (const Error &error) {
        if (error.code != "physical-cloth-missing")
            throw;
        out.lods_.clear();
        out.configs_.clear();
        out.diagnostics_.push_back(error.what());
    }
    o.control.checkpoint("physical-cloth", r.values, r.values);
    return out;
}
Json PhysicalClothAsset::to_json(bool include, const OperationControl &control) const {
    Report r{report_values_, control};
    r.use(64 + lod_map_.size() + bone_indices_.size() + bone_names_.size());
    Json out = {{"schema", "cyuediff.physical-cloth/1"},
                {"profile", "ue5.3-windows"},
                {"scope", "stored-physical-cloth"},
                {"object", object_},
                {"available", available_},
                {"assetGuid", guid_},
                {"lodMap", lod_map_},
                {"usedBoneIndices", bone_indices_},
                {"usedBoneNames", bone_names_},
                {"referenceBoneIndex", reference_bone_ ? Json(*reference_bone_) : Json(nullptr)},
                {"configMapReplaced", configs_replaced_},
                {"removedConfigEntries", removed_configs_},
                {"defaultsResolved", false},
                {"simulationEvaluated", false},
                {"provenance",
                 {{"packageSha256", package_sha_},
                  {"schemaContextSha256", context_sha_},
                  {"scriptObjectsSha256", scripts_sha_}}},
                {"diagnostics", diagnostics_},
                {"configs", Json::array()},
                {"lods", Json::array()}};
    for (const auto &config : configs_) {
        r.use(32);
        out["configs"].push_back({{"key", config.key},
                                  {"object", config.object_path},
                                  {"class", config.class_name},
                                  {"available", config.available},
                                  {"values", config.values},
                                  {"unavailableFields", config.unavailable_fields}});
    }
    for (const auto &l : lods_) {
        r.use(64);
        Json j = {{"vertices", l.vertices.size()},
                  {"normals", l.normals.size()},
                  {"triangles", l.indices.size() / 3},
                  {"inverseMasses", l.inverse_masses.size()},
                  {"boneData", l.bones.size()},
                  {"weightMapReplaced", l.weight_maps_replaced},
                  {"removedWeightMapEntries", l.removed_weight_maps},
                  {"bounds", l.bounds},
                  {"geometryAvailable", bool(l.geometry)},
                  {"parameters", l.parameters},
                  {"transitionUp", transition_json(l.up, include, r)},
                  {"transitionDown", transition_json(l.down, include, r)}};
        auto batches = [&](const auto &b) {
            Json rows = Json::array();
            for (const auto &batch : b) {
                r.use(1);
                if (include) {
                    Json values = Json::array();
                    for (const auto &t : batch) {
                        r.use(4);
                        values.push_back(
                            {{"anchor", t.anchor}, {"particle", t.particle}, {"length", t.length}});
                    }
                    rows.push_back(std::move(values));
                } else
                    rows.push_back(batch.size());
            }
            return rows;
        };
        j["euclideanTethers"] = batches(l.euclidean_tethers);
        j["geodesicTethers"] = batches(l.geodesic_tethers);
        j["weightMaps"] = Json::object();
        for (const auto &w : l.weight_maps) {
            r.use(2);
            j["weightMaps"][std::to_string(w.first)] = include ? r.array(w.second) : Json(w.second.size());
        }
        if (include) {
            j["vertices"] = r.array(l.vertices, 4);
            j["normals"] = r.array(l.normals, 4);
            j["indices"] = r.array(l.indices);
            j["inverseMasses"] = r.array(l.inverse_masses);
            j["selfCollisionIndices"] = r.array(l.self_collision_indices);
            j["boneData"] = Json::array();
            for (const auto &b : l.bones) {
                r.use(30);
                j["boneData"].push_back(bone_json(b));
            }
        }
        out["lods"].push_back(std::move(j));
    }
    return out;
}
Json PhysicalClothAsset::vertex(std::uint32_t lod, std::uint32_t vertex, const OperationControl &c) const {
    c.check();
    const auto &l = select(*this, lod);
    if (vertex >= l.vertices.size())
        throw Error("physical-cloth-selection", "Physical vertex exceeds LOD");
    Json out = {{"vertex", vertex}, {"position", l.vertices[vertex]}};
    if (!l.normals.empty())
        out["normal"] = l.normals[vertex];
    if (!l.inverse_masses.empty())
        out["inverseMass"] = l.inverse_masses[vertex];
    if (!l.bones.empty())
        out["boneData"] = bone_json(l.bones[vertex]);
    out["weightMaps"] = Json::object();
    for (const auto &map : l.weight_maps) {
        c.check();
        if (!map.second.empty())
            out["weightMaps"][std::to_string(map.first)] = map.second[vertex];
    }
    return out;
}
Json PhysicalClothAsset::transition(std::uint32_t lod, bool up, std::uint32_t vertex,
                                    const OperationControl &c) const {
    c.check();
    const auto &l = select(*this, lod);
    const auto &t = up ? l.up : l.down;
    if (vertex >= t.records.size())
        throw Error("physical-cloth-selection", "Physical transition vertex is unavailable");
    auto value = t.records[vertex].to_json(true);
    value["sourceLOD"] = t.source_lod ? Json(*t.source_lod) : Json(nullptr);
    value["sourceBoundsVerified"] = t.source_bounds_verified;
    value["wrapAvailable"] = t.wrap_available;
    return value;
}
std::vector<std::array<float, 3>> PhysicalClothAsset::wrap_transition(std::uint32_t target, bool up,
                                                                      const OperationControl &c) const {
    c.check();
    const auto &l = select(*this, target);
    const auto &t = up ? l.up : l.down;
    if (!t.wrap_available || !t.source_lod)
        throw Error("physical-cloth-unavailable", "Physical transition has no verified source coordinates");
    const auto &source = lods_.at(*t.source_lod);
    std::vector<std::array<float, 3>> result;
    result.reserve(t.records.size());
    for (std::size_t i = 0; i < t.records.size(); ++i) {
        if (!(i & 255))
            c.check();
        const auto &m = t.records[i];
        std::array<float, 3> value{};
        for (unsigned k = 0; k < 3; ++k) {
            for (unsigned j = 0; j < 3; ++j) {
                const auto id = m.source_indices[j];
                const float p = source.vertices[id][k] * m.position[j],
                            n = source.normals[id][k] * m.position[3];
                if (j == 0)
                    value[k] = p;
                else
                    value[k] += p;
                value[k] += n;
            }
            need(std::isfinite(value[k]), "Non-finite physical transition output");
        }
        result.push_back(value);
    }
    c.check();
    return result;
}
} // namespace cyuediff
