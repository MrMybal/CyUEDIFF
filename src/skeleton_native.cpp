#include "skeleton_native.hpp"
#include <cmath>
#include <set>
namespace cyuediff::detail {
NativeDecodeResult decode_reference_skeleton(Archive &a, const Package &package, std::uint64_t end,
                                             const ReadOptions &options, bool skeletal_mesh) {
    const auto &summary = package.summary;
    if (summary.ue4_version != 522 || (summary.ue5_version != 1004 && summary.ue5_version != 1013) ||
        summary.licensee_version || (summary.flags & 0x200u))
        throw Error("skeleton-profile", "Unsupported reference skeleton package profile", a.tell());
    if (skeletal_mesh) {
        auto version = [&](const char *guid, int low, int high) {
            const auto it = summary.custom_versions.find(guid);
            return it != summary.custom_versions.end() && it->is_number_integer() &&
                   it->get<std::int64_t>() >= low && it->get<std::int64_t>() <= high;
        };
        if (!version("e4b068edf49442e9a231da0b2e46bb41", 8, 40) ||
            !version("375ec13c06e448fbb50084f0262a717e", 3, 4) ||
            !version("12f88b9f88754afca67cd90c383abd29", 10, 45))
            throw Error("skeletal-mesh-profile", "Unsupported skeletal material custom versions", a.tell());
    }
    const auto start = a.tell();
    Archive::ReadLimit window(a, start + std::min(end - start, options.max_property_bytes));
    std::uint32_t work = 0;
    std::uint64_t text_bytes = 0;
    auto count = [&] {
        const auto n = a.get<std::int32_t>();
        if (n < 0 || std::uint32_t(n) > options.max_table_entries ||
            std::uint32_t(n) > options.max_properties - work)
            throw Error("limit", "Reference skeleton aggregate work budget exceeded", a.tell() - 4);
        work += std::uint32_t(n);
        return std::uint32_t(n);
    };
    auto charge = [&](std::size_t size) {
        if (size > options.max_property_bytes - text_bytes)
            throw Error("limit", "Reference skeleton text budget exceeded", a.tell());
        text_bytes += size;
    };
    auto name = [&] {
        const auto index = a.get<std::int32_t>(), number = a.get<std::int32_t>();
        if (index < 0 || std::size_t(index) >= package.names.size() || number < 0)
            throw Error("skeleton-name", "Invalid stored bone FName", a.tell() - 8);
        auto value = package.names[index] + (number ? "_" + std::to_string(number - 1) : "");
        charge(value.size());
        return value;
    };
    const auto has_guid = a.get<std::uint32_t>();
    if (has_guid > 1)
        throw Error("skeleton-guid", "Invalid UObject GUID presence marker", a.tell() - 4);
    const auto object_guid = has_guid ? a.guid() : std::string{};
    auto transform = [&] {
        Json pose = Json::object();
        for (auto field : {std::make_pair("rotation", 4u), std::make_pair("translation", 3u),
                           std::make_pair("scale", 3u)}) {
            Json values = Json::array();
            for (unsigned k = 0; k < field.second; ++k) {
                const auto x = a.get<double>();
                if (!std::isfinite(x))
                    throw Error("skeleton-pose", "Non-finite stored reference pose", a.tell() - 8);
                values.push_back(x);
            }
            pose[field.first] = std::move(values);
        }
        return pose;
    };
    Json mesh = Json::object();
    if (skeletal_mesh) {
        const auto global = a.get<std::uint8_t>(), specific = a.get<std::uint8_t>();
        if (global & ~3u)
            throw Error("skeletal-mesh-strip", "Unknown skeletal mesh global strip flags", a.tell() - 2);
        mesh["stripFlags"] = {{"global", global}, {"class", specific}};
        Json bounds = Json::object();
        for (auto field : {std::make_pair("origin", 3u), std::make_pair("boxExtent", 3u),
                           std::make_pair("sphereRadius", 1u)}) {
            Json values = Json::array();
            for (unsigned k = 0; k < field.second; ++k) {
                const auto x = a.get<double>();
                if (!std::isfinite(x))
                    throw Error("skeletal-mesh-bounds", "Non-finite imported bounds", a.tell() - 8);
                values.push_back(x);
            }
            bounds[field.first] = field.second == 1 ? values[0] : values;
        }
        mesh["importedBounds"] = std::move(bounds);
        auto boolean = [&] {
            const auto value = a.get<std::uint32_t>();
            if (value > 1)
                throw Error("skeletal-mesh-boolean", "Invalid native material boolean", a.tell() - 4);
            return value != 0;
        };
        Json materials = Json::array();
        const auto material_count = count();
        for (std::uint32_t i = 0; i < material_count; ++i) {
            options.control.checkpoint("Reading skeletal mesh materials", i, material_count);
            const auto material = package.reference(a.get<std::int32_t>());
            charge(material.size());
            Json entry = {{"material", material}, {"slotName", name()}};
            const bool imported = boolean();
            entry["importedSlotNameStored"] = imported;
            if (imported)
                entry["importedSlotName"] = name();
            Json uv = {{"initialized", boolean()}, {"overrideDensities", boolean()}};
            Json densities = Json::array();
            for (unsigned k = 0; k < 4; ++k) {
                const auto density = a.get<float>();
                if (!std::isfinite(density))
                    throw Error("skeletal-mesh-uv", "Non-finite stored UV density", a.tell() - 4);
                densities.push_back(density);
            }
            uv["localDensities"] = std::move(densities);
            entry["uvChannels"] = std::move(uv);
            materials.push_back(std::move(entry));
        }
        mesh["materials"] = std::move(materials);
    }
    const bool export_names = !(summary.flags & 0x80000000u);
    Json bones = Json::array(), poses = Json::array(), lookup = Json::object();
    std::set<std::string> names;
    auto n = count();
    for (std::uint32_t i = 0; i < n; ++i) {
        options.control.checkpoint("Reading reference skeleton", i, n);
        auto key = name();
        const auto parent = a.get<std::int32_t>();
        if (key.empty() || key == "None" || !names.insert(key).second || parent < -1 ||
            parent >= std::int64_t(i))
            throw Error("skeleton-hierarchy", "Invalid stored bone identity or parent order", a.tell());
        Json bone = {{"name", key}, {"parentIndex", parent}};
        if (export_names) {
            auto exported = a.string();
            charge(exported.size());
            bone["exportName"] = std::move(exported);
        }
        bones.push_back(std::move(bone));
    }
    const auto pose_count = count();
    if (pose_count != n)
        throw Error("skeleton-pose", "Bone and pose counts differ", a.tell() - 4);
    for (std::uint32_t i = 0; i < n; ++i) {
        options.control.check();
        poses.push_back(transform());
    }
    const auto map_count = count();
    if (map_count != n)
        throw Error("skeleton-map", "Stored bone map count differs", a.tell() - 4);
    for (std::uint32_t i = 0; i < n; ++i) {
        options.control.check();
        auto key = name();
        const auto index = a.get<std::int32_t>();
        if (index < 0 || std::uint32_t(index) >= n || bones[index].at("name") != key || lookup.contains(key))
            throw Error("skeleton-map", "Stored bone map is inconsistent", a.tell());
        lookup[key] = index;
    }
    Json decoded = {
        {"kind", "reference-skeleton"},
        {"layout", skeletal_mesh ? "ue5-skeletal-mesh-reference-prefix" : "ue5-raw-reference-prefix"},
        {"bones", std::move(bones)},
        {"localPoses", std::move(poses)},
        {"nameToIndex", std::move(lookup)},
        {"exportNamesStored", export_names}};
    if (skeletal_mesh)
        decoded["mesh"] = std::move(mesh);
    if (has_guid)
        decoded["objectGuid"] = object_guid;
    const auto reference_end = a.tell();
    if (!skeletal_mesh) {
        decoded["retargeting"] = {{"coverage", options.native_skeleton_retargeting ? "opaque" : "disabled"}};
        if (options.native_skeleton_retargeting) {
            try {
                const auto source_count = count();
                const auto custom = summary.custom_versions.find("29e575dde0a346279d10d276232cdcea");
                int anim_phys = -1;
                if (custom != summary.custom_versions.end() && custom->is_number_integer() &&
                    custom->get<std::int64_t>() >= 0 && custom->get<std::int64_t>() <= 17)
                    anim_phys = custom->get<int>();
                if (source_count && anim_phys < 0)
                    throw Error("skeleton-retarget-profile",
                                "Nonempty retarget sources require a supported AnimPhys version", a.tell());
                Json sources = Json::object();
                for (std::uint32_t i = 0; i < source_count; ++i) {
                    options.control.checkpoint("Reading skeleton retarget sources", i, source_count);
                    const auto key = name();
                    if (key.empty() || sources.contains(key))
                        throw Error("skeleton-retarget-identity", "Empty or duplicate retarget source key",
                                    a.tell());
                    Json source = {{"poseName", name()}};
                    const auto transforms = count();
                    Json local = Json::array();
                    for (std::uint32_t j = 0; j < transforms; ++j) {
                        options.control.check();
                        local.push_back(transform());
                    }
                    source["localPoses"] = std::move(local);
                    if (!(summary.flags & 0x80000000u)) {
                        Json mesh_ref;
                        if (anim_phys < 15) {
                            const auto path = package.reference(a.get<std::int32_t>());
                            charge(path.size());
                            mesh_ref = {{"kind", "object"}, {"path", path}};
                        } else {
                            auto path = name();
                            if (summary.ue5_version >= 1007) {
                                const auto asset = name();
                                if (asset != "None")
                                    path += "." + asset;
                            }
                            const auto sub = a.string();
                            charge(sub.size());
                            if (!sub.empty())
                                path += ":" + sub;
                            charge(path.size());
                            mesh_ref = {{"kind", "soft-object"}, {"path", std::move(path)}};
                        }
                        source["sourceReferenceMesh"] = std::move(mesh_ref);
                    }
                    sources[key] = std::move(source);
                }
                const auto guid = a.guid();
                decoded["retargeting"] = {
                    {"coverage", "stored"}, {"sources", std::move(sources)}, {"skeletonGuid", guid}};
            } catch (const Error &error) {
                if (error.code == "cancelled")
                    throw;
                a.seek(reference_end);
                decoded["retargeting"] = {{"coverage", "opaque"}, {"reason", error.code}};
            }
        }
    }
    return {std::move(decoded), a.tell()};
}
} // namespace cyuediff::detail
