#include "skeleton_native.hpp"
#include <cmath>
#include <set>
namespace cyuediff::detail {
NativeDecodeResult decode_reference_skeleton(Archive &a, const Package &package, std::uint64_t end,
                                             const ReadOptions &options) {
    const auto &summary = package.summary;
    if (summary.ue4_version != 522 || (summary.ue5_version != 1004 && summary.ue5_version != 1013) ||
        summary.licensee_version || (summary.flags & 0x200u))
        throw Error("skeleton-profile", "Unsupported reference skeleton package profile", a.tell());
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
        poses.push_back(std::move(pose));
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
    Json decoded = {{"kind", "reference-skeleton"},     {"layout", "ue5-raw-reference-prefix"},
                    {"bones", std::move(bones)},        {"localPoses", std::move(poses)},
                    {"nameToIndex", std::move(lookup)}, {"exportNamesStored", export_names}};
    if (has_guid)
        decoded["objectGuid"] = object_guid;
    return {std::move(decoded), a.tell()};
}
} // namespace cyuediff::detail
