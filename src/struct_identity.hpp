#pragma once
#include <cyuediff/tagged_types.hpp>
#include <set>
namespace cyuediff::detail {
inline const std::string &zero_struct_guid() {
    static const std::string value(32, '0');
    return value;
}
inline bool canonical_struct_guid(const std::string &value) {
    return value.size() == 32 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
// Supported standard writer forms. The raw full-type parameter is retained separately.
inline std::string normalized_struct_guid(const std::string &value) {
    const bool dashed = value.size() == 36;
    if (!dashed && value.size() != 32)
        return {};
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (dashed && (i == 8 || i == 13 || i == 18 || i == 23)) {
            if (value[i] != '-')
                return {};
            continue;
        }
        auto c = value[i];
        if (c >= 'A' && c <= 'F')
            c += 'a' - 'A';
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return {};
        result += c;
    }
    return result;
}
inline std::string struct_symbol_owner(const TaggedType &symbol, bool strict = true) {
    auto fail = [&]() -> std::string {
        if (strict)
            throw Error("struct-identity", "Unsupported stored struct owner qualifiers");
        return {};
    };
    if (symbol.name.empty() || symbol.name.size() > 4096)
        return fail();
    if (symbol.parameters.empty())
        return symbol.name.find_first_of("./") == std::string::npos ? std::string{} : symbol.name;
    std::string owner;
    for (const auto &part : symbol.parameters) {
        if (!part.parameters.empty() || part.name.empty() || part.name.size() > 4096 - owner.size())
            return fail();
        if (!owner.empty())
            owner += '.';
        if (part.name.size() > 4096 - owner.size())
            return fail();
        owner += part.name;
    }
    if (owner.empty() || owner.front() != '/' || symbol.name.size() + 1 > 4096 - owner.size())
        return fail();
    return owner + '.' + symbol.name;
}
struct StructIdentity {
    std::string name, owner, guid;
    bool custom_guid() const { return !guid.empty() && guid != zero_struct_guid(); }
};
inline StructIdentity struct_identity(const TaggedType &type) {
    if (type.parameters.empty() || type.parameters.size() > 2)
        throw Error("struct-identity", "Unsupported stored struct identity parameters");
    StructIdentity result;
    const auto &symbol = type.parameters[0];
    result.owner = struct_symbol_owner(symbol);
    const auto split = symbol.name.find_last_of("./");
    result.name = split == std::string::npos ? symbol.name : symbol.name.substr(split + 1);
    if (!type.schema.empty() && type.schema.find_first_of("./") != std::string::npos) {
        if (!result.owner.empty() && result.owner != type.schema)
            throw Error("tagged-types-conflict", "Stored struct owner contradicts metadata");
        result.owner = type.schema;
    }
    result.guid = type.struct_guid;
    if (!result.guid.empty() && !canonical_struct_guid(result.guid))
        throw Error("struct-identity", "Invalid canonical struct GUID");
    if (type.parameters.size() == 2) {
        const auto &part = type.parameters[1];
        const auto guid = normalized_struct_guid(part.name);
        if (!part.parameters.empty() || guid.empty())
            throw Error("struct-identity", "Unsupported full-type struct GUID parameter");
        if (!result.guid.empty() && result.guid != guid)
            throw Error("struct-identity", "Stored struct GUID representations disagree");
        result.guid = guid;
    }
    return result;
}
inline std::string builtin_struct_owner(const std::string &name) {
    static const std::set<std::string> core = {
        "Guid",   "DateTime", "Timespan",    "Box",       "Box3f",       "Box3d",
        "Sphere", "Sphere3f", "Sphere3d",    "Matrix",    "Matrix44f",   "Matrix44d",
        "Plane",  "Plane4f",  "Plane4d",     "Transform", "Transform3f", "Transform3d",
        "Vector", "Rotator",  "Vector3f",    "Vector3d",  "Vector2D",    "Quat",
        "Quat4f", "Vector4",  "LinearColor", "IntPoint",  "IntVector",   "Color"};
    if (core.count(name))
        return "/Script/CoreUObject." + name;
    if (name == "RichCurveKey" || name == "RawAnimSequenceTrack")
        return "/Script/Engine." + name;
    return {};
}
} // namespace cyuediff::detail
